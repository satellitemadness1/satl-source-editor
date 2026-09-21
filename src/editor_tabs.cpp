#include "editor_tabs.hpp"
#include "gtk_host.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace satellite {
namespace {

// Until the Satellite language definition lands, something to look at in
// the document the editor opens with.
const char *const kWelcome =
    "satellite.include(satellite)\n"
    "\n"
    "satellite.capsule satellite.main(satellite.container.list<satellite.variable.string> arguments)\n"
    "{\n"
    "    satellite.console.display(\"hello, claude!\")\n"
    "\n"
    "    satellite.return(satellite)\n"
    "}\n"
    "\n"
    "// this is a comment\n";

// Which page a tab's close button belongs to, hung on the button itself so
// that dragging tabs into a different order still closes the right one.
const char *const kPageKey = "satl-page";

} // namespace

EditorTabs::EditorTabs()
    : Actor("editor-tabs"),
      m_alive(std::make_shared<std::atomic<bool>>(true))
{
    m_notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(m_notebook, TRUE);
    gtk_widget_set_vexpand(m_notebook, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(m_notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(m_notebook), FALSE);

    // Parked at the end of the strip: without it there would be no way to
    // reach a second untitled document. It says so in words rather than
    // wearing a "+", at the user's request.
    GtkWidget *plus = gtk_button_new_with_label("NEW TAB");
    gtk_widget_add_css_class(plus, "flat");
    gtk_widget_set_focus_on_click(plus, FALSE);
    gtk_widget_set_valign(plus, GTK_ALIGN_CENTER);
    g_signal_connect(plus, "clicked",
                     G_CALLBACK(+[](GtkButton *, gpointer data)
                                { static_cast<EditorTabs *>(data)->open_untitled(); }),
                     this);

    // ...and beside it, a whole second copy of the program.
    GtkWidget *window_button = gtk_button_new_with_label("NEW WINDOW");
    gtk_widget_add_css_class(window_button, "flat");
    gtk_widget_set_focus_on_click(window_button, FALSE);
    gtk_widget_set_valign(window_button, GTK_ALIGN_CENTER);

    g_signal_connect(window_button, "clicked",
                     G_CALLBACK(+[](GtkButton *, gpointer data)
                                {
                                    auto *self = static_cast<EditorTabs *>(data);
                                    if (self->m_on_new_window)
                                        self->m_on_new_window();
                                }),
                     this);

    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_append(GTK_BOX(actions), plus);
    gtk_box_append(GTK_BOX(actions), window_button);
    gtk_widget_set_margin_start(actions, 4);
    gtk_widget_set_margin_end(actions, 4);

    gtk_notebook_set_action_widget(GTK_NOTEBOOK(m_notebook), actions, GTK_PACK_END);

    // Switching tabs changes what a restart would put back, so it counts.
    g_signal_connect(m_notebook, "switch-page",
                     G_CALLBACK(+[](GtkNotebook *, GtkWidget *, guint, gpointer data)
                                { static_cast<EditorTabs *>(data)->notify_changed(); }),
                     this);

    // No document yet on purpose. The shell either puts a remembered
    // session back or asks for the welcome one; starting with an untitled
    // tab here would make a restored window flash a document it never had.
}

EditorTabs::~EditorTabs()
{
    // This widget's own thread first, while the members it reads are alive.
    stop();

    // Anything still queued on the GTK thread now becomes a no-op.
    m_alive->store(false);

    // Each view stops its own thread as it goes.
    m_tabs.clear();
}

EditorTabs::Tab *EditorTabs::find_by_path(const std::string &path)
{
    const auto it = std::find_if(m_tabs.begin(), m_tabs.end(),
                                 [&path](const std::unique_ptr<Tab> &tab)
                                 { return tab->path == path; });

    return it == m_tabs.end() ? nullptr : it->get();
}

// "Untitled1.txt", then "Untitled2.txt", and so on -- reusing the lowest
// number nothing is currently called, so closing tabs does not send the
// count marching off into the distance.
std::string EditorTabs::next_untitled_name() const
{
    for (int n = 1;; ++n)
    {
        const std::string candidate = "Untitled" + std::to_string(n) + ".txt";

        const bool taken = std::any_of(m_tabs.begin(), m_tabs.end(),
                                       [&candidate](const std::unique_ptr<Tab> &tab)
                                       { return tab->title == candidate; });

        if (!taken)
            return candidate;
    }
}

GtkWidget *EditorTabs::build_tab_label(Tab &tab)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    tab.label = gtk_label_new(tab.title.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(tab.label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(tab.label), 24);
    gtk_label_set_single_line_mode(GTK_LABEL(tab.label), TRUE);

    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close, "flat");
    gtk_widget_set_focus_on_click(close, FALSE);
    gtk_widget_set_valign(close, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(close, "Close document");

    g_object_set_data(G_OBJECT(close), kPageKey, tab.child);
    g_signal_connect(close, "clicked",
                     G_CALLBACK(+[](GtkButton *button, gpointer data)
                                {
                                    auto *self = static_cast<EditorTabs *>(data);
                                    auto *page = static_cast<GtkWidget *>(
                                        g_object_get_data(G_OBJECT(button), kPageKey));
                                    self->close_tab(page);
                                }),
                     this);

    gtk_box_append(GTK_BOX(box), tab.label);
    gtk_box_append(GTK_BOX(box), close);

    return box;
}

void EditorTabs::add_tab(std::string path, std::string title, const std::string &text,
                         bool take_focus)
{
    auto tab = std::make_unique<Tab>();

    // Every document carries a view, and every view carries a thread of its
    // own: that is where the file is read.
    tab->view = std::make_unique<SourceEditorView>();
    tab->child = tab->view->widget();
    tab->path = std::move(path);
    tab->title = std::move(title);

    GtkWidget *label = build_tab_label(*tab);
    if (!tab->path.empty())
        gtk_widget_set_tooltip_text(label, tab->path.c_str());

    const int page = gtk_notebook_append_page(GTK_NOTEBOOK(m_notebook), tab->child, label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(m_notebook), tab->child, TRUE);

    if (!text.empty())
        tab->view->set_text(text);

    if (!tab->path.empty())
        tab->view->load_file(tab->path);

    SourceEditorView *view = tab->view.get();
    m_tabs.push_back(std::move(tab));

    if (page >= 0)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), page);

    // grab_focus() does nothing at all until the widget is part of a
    // window, so the very first tab -- built while the notebook is still
    // being assembled -- is focused later, by the shell.
    if (take_focus && gtk_widget_get_root(m_notebook) != nullptr)
        view->focus();

    notify_changed();
}

void EditorTabs::raise(const Tab &tab)
{
    const int page = gtk_notebook_page_num(GTK_NOTEBOOK(m_notebook), tab.child);
    if (page >= 0)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook), page);

    tab.view->focus();
}

void EditorTabs::close_tab(GtkWidget *child)
{
    const auto it = std::find_if(m_tabs.begin(), m_tabs.end(),
                                 [child](const std::unique_ptr<Tab> &tab)
                                 { return tab->child == child; });

    if (it == m_tabs.end())
        return;

    // Off the list before anything is destroyed, so nothing can find this
    // tab half-dismantled.
    std::unique_ptr<Tab> going = std::move(*it);
    m_tabs.erase(it);

    const int page = gtk_notebook_page_num(GTK_NOTEBOOK(m_notebook), child);
    if (page >= 0)
        gtk_notebook_remove_page(GTK_NOTEBOOK(m_notebook), page);

    // Then the view, which stops and joins its own thread.
    going.reset();

    // An empty notebook is a hole in the window; keep a document in it.
    // Without taking the keyboard, though: closing the last tab is no
    // reason to pull the cursor out of the path entry or the search bar.
    if (m_tabs.empty())
        open_untitled(std::string(), false);

    notify_changed();
}

void EditorTabs::set_on_new_window(std::function<void()> fn)
{
    m_on_new_window = std::move(fn);
}

void EditorTabs::set_on_changed(std::function<void()> fn)
{
    m_on_changed = std::move(fn);
}

void EditorTabs::notify_changed()
{
    if (!m_restoring && m_on_changed)
        m_on_changed();
}

void EditorTabs::open_welcome()
{
    open_untitled(kWelcome);
}

std::vector<EditorTabs::OpenTab> EditorTabs::open_tabs() const
{
    std::vector<OpenTab> open;

    // Page order, not the order they were created in: tabs can be dragged
    // about, and what comes back should be what was on screen.
    const int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(m_notebook));
    open.reserve(static_cast<std::size_t>(pages));

    for (int page = 0; page < pages; ++page)
    {
        const GtkWidget *child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), page);

        for (const auto &tab : m_tabs)
            if (tab->child == child)
            {
                open.push_back(OpenTab{tab->path});
                break;
            }
    }

    return open;
}

int EditorTabs::current_index() const
{
    const int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    return page < 0 ? 0 : page;
}

bool EditorTabs::restore(const std::vector<OpenTab> &tabs, int current)
{
    m_restoring = true;

    for (const auto &tab : tabs)
    {
        if (tab.path.empty())
        {
            open_untitled(std::string(), false);
            continue;
        }

        // Deleted since the window was last closed: drop it rather than
        // opening a tab that can only say the file is missing.
        if (!g_file_test(tab.path.c_str(), G_FILE_TEST_IS_REGULAR))
            continue;

        add_tab(tab.path, std::filesystem::path(tab.path).filename().string(),
                std::string(), false);
    }

    const int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(m_notebook));
    if (pages > 0)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(m_notebook),
                                      current >= 0 && current < pages ? current : 0);

    m_restoring = false;

    return pages > 0;
}

void EditorTabs::open_untitled(const std::string &initial_text, bool take_focus)
{
    add_tab(std::string(), next_untitled_name(), initial_text, take_focus);
}

void EditorTabs::focus_current()
{
    const int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(m_notebook));
    if (page < 0)
        return;

    GtkWidget *child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(m_notebook), page);

    for (const auto &tab : m_tabs)
        if (tab->child == child)
        {
            tab->view->focus();
            return;
        }
}

void EditorTabs::open_file(std::string path)
{
    // Resolving the path touches the filesystem, so it happens on this
    // widget's thread rather than on the one drawing the window.
    post([this, alive = m_alive, path = std::move(path)]
         {
             std::error_code ec;

             const auto resolved = std::filesystem::canonical(path, ec);
             if (ec)
             {
                 // Gone from the disk -- but a tab may still be holding it,
                 // and raising that is more use than doing nothing.
                 GtkHost::post([this, alive, full = path]
                               {
                                   if (!alive->load())
                                       return;

                                   if (Tab *open = find_by_path(full))
                                       raise(*open);
                               });
                 return;
             }

             const bool regular = std::filesystem::is_regular_file(resolved, ec);
             if (ec || !regular)
                 return;

             GtkHost::post([this, alive, full = resolved.string()]
                           {
                               if (!alive->load())
                                   return;

                               // Already open: raise that tab instead of
                               // opening the same file a second time.
                               if (Tab *open = find_by_path(full))
                               {
                                   raise(*open);
                                   return;
                               }

                               add_tab(full,
                                       std::filesystem::path(full).filename().string(),
                                       std::string(), true);
                           });
         });
}

} // namespace satellite
