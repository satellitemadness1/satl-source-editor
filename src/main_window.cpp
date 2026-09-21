#include "main_window.hpp"

#include <filesystem>

namespace satellite {
namespace {

// How much of the window the directory pane takes down the left.
constexpr int kDirectoryPaneWidth = 250;

// NEW WINDOW is a second copy of the program rather than a second window
// inside this one: the process boundary is the isolation, and a window that
// wedges or crashes takes nothing else with it.
//
// It starts in whatever directory the pane is showing, so the new window
// opens looking at the same place this one is.
//
// (This only works because the application is created NON_UNIQUE -- see
// gtk_host.cpp. With an id and the default flags, the second copy hands its
// activation to the first over D-Bus and exits without drawing anything.)
void spawn_new_window(const std::string &directory)
{
    // Whatever this binary actually is, wherever it was installed and
    // whatever name it was invoked under.
    char *exe = g_file_read_link("/proc/self/exe", nullptr);

    char *argv[] = {exe != nullptr ? exe : const_cast<char *>("satl-source"), nullptr};

    GError *error = nullptr;
    const gboolean spawned =
        g_spawn_async(directory.empty() ? nullptr : directory.c_str(),
                      argv, nullptr, G_SPAWN_SEARCH_PATH,
                      nullptr, nullptr, nullptr, &error);

    if (!spawned)
    {
        g_warning("could not open a new window: %s",
                  error != nullptr ? error->message : "unknown error");
        g_clear_error(&error);
    }

    g_free(exe);
}

} // namespace

MainWindow::MainWindow()
{
    set_title("Satellite Source Editor");
    set_default_size(1100, 760);
    maximize();

    // Which window is this? Settled with the other running copies of the
    // program, before anything is put on screen.
    m_session = std::make_unique<Session>();
    const bool remembered = m_session->claim_slot();

    m_tabs = std::make_unique<EditorTabs>();
    m_directory = std::make_unique<DirectoryView>();

    // Double-clicking a file in the pane opens it in the tabs -- or raises
    // the tab it is already open in.
    m_directory->set_on_file_activated([tabs = m_tabs.get()](std::string path)
                                       { tabs->open_file(std::move(path)); });

    // The tabs know nothing about windows or processes; the shell does.
    m_tabs->set_on_new_window([directory = m_directory.get()]
                              { spawn_new_window(directory->shown_directory()); });

    // The search bar searches wherever the pane currently is, and opens
    // what it finds in a tab.
    m_search = std::make_unique<SearchBar>();
    m_search->set_root_provider([directory = m_directory.get()]
                                { return directory->shown_directory(); });
    m_search->set_on_activated([tabs = m_tabs.get()](std::string path)
                               { tabs->open_file(std::move(path)); });

    // Crossing from gtkmm into the C API through gobj() -- the safe
    // direction. (Glib::wrap the other way has no wrapper to hand back
    // for a GtkSourceView.)
    GtkWidget *split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(split), m_directory->widget());
    // The editing half, top to bottom: the search bar, then the tabs. Both
    // sit inside the paned's end child, so neither reaches across the
    // directory pane.
    GtkWidget *editing_half = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(editing_half), m_search->widget());
    gtk_box_append(GTK_BOX(editing_half), m_tabs->widget());

    gtk_paned_set_end_child(GTK_PANED(split), editing_half);
    gtk_paned_set_position(GTK_PANED(split), kDirectoryPaneWidth);

    // The tabs and the search bar live entirely inside the end child, so
    // both span the editing half and stop where the directory pane begins.
    //
    // The editor absorbs the extra width; the pane keeps the 250 it started
    // with, but stays draggable.
    gtk_paned_set_resize_start_child(GTK_PANED(split), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(split), FALSE);
    gtk_paned_set_resize_end_child(GTK_PANED(split), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(split), FALSE);

    gtk_window_set_child(GTK_WINDOW(gobj()), split);

    // Put back what this window had open when it was last closed. Failing
    // that -- a fresh slot, or every remembered file since deleted -- the
    // window opens on the welcome program, as it always did.
    bool restored = false;

    if (remembered)
    {
        const Session::State state = m_session->load();

        std::vector<EditorTabs::OpenTab> tabs;
        tabs.reserve(state.tabs.size());
        for (const auto &tab : state.tabs)
            tabs.push_back(EditorTabs::OpenTab{tab.path});

        restored = m_tabs->restore(tabs, state.current);
    }

    if (!restored)
        m_tabs->open_welcome();

    // From here on, every change to what is open is written down. Connected
    // after the restore, which is not a change.
    m_tabs->set_on_changed([this] { remember_session(); });

    // Once at the start too, so that a window nobody touches still records
    // what it is showing -- and, on closing, when it was closed.
    remember_session();

    // Only now is the notebook inside a window, so only now can a document
    // take the keyboard.
    m_tabs->focus_current();



    std::error_code ec;
    const auto here = std::filesystem::current_path(ec);
    if (!ec)
        m_directory->open_directory(here.string());




}

void MainWindow::remember_session()
{
    if (!m_session || !m_tabs)
        return;

    Session::State state;

    for (const auto &tab : m_tabs->open_tabs())
        state.tabs.push_back(Session::Tab{tab.path});

    state.current = m_tabs->current_index();

    m_session->save(std::move(state));
}

} // namespace satellite
