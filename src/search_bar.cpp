#include "search_bar.hpp"
#include "gtk_host.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace satellite {
namespace {

// Enough results to be useful, few enough that the list stays a list.
constexpr std::size_t kMaxMatches = 200;

// A hard stop on the walk itself: pointed at a huge tree, the search says
// what it managed rather than reading the whole disk.
constexpr std::size_t kMaxVisited = 200000;

// How tall the results are allowed to grow before they scroll.
constexpr int kResultsHeight = 240;

// The path a row stands for.
const char *const kResultPathKey = "satl-result";

std::string lowercase(const std::string &text)
{
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

SearchBar::SearchBar()
    : Actor("search-bar"),
      m_alive(std::make_shared<std::atomic<bool>>(true))
{
    // A search entry rather than a plain one: it carries the magnifier and
    // the clear button, and it holds each keystroke back briefly so that
    // typing a word does not start a walk per letter.
    m_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(m_entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(m_entry), "");
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(m_entry),
                                          "search this directory and everything in it…");
    gtk_widget_set_margin_start(m_entry, 6);
    gtk_widget_set_margin_end(m_entry, 6);
    gtk_widget_set_margin_top(m_entry, 6);
    gtk_widget_set_margin_bottom(m_entry, 6);

    g_signal_connect(m_entry, "search-changed",
                     G_CALLBACK(+[](GtkSearchEntry *, gpointer data)
                                { static_cast<SearchBar *>(data)->on_search_changed(); }),
                     this);

    // Enter takes the first result.
    g_signal_connect(m_entry, "activate",
                     G_CALLBACK(+[](GtkSearchEntry *, gpointer data)
                                { static_cast<SearchBar *>(data)->on_entry_activated(); }),
                     this);

    // Escape puts the editor back.
    g_signal_connect(m_entry, "stop-search",
                     G_CALLBACK(+[](GtkSearchEntry *, gpointer data)
                                {
                                    auto *self = static_cast<SearchBar *>(data);
                                    gtk_editable_set_text(GTK_EDITABLE(self->m_entry), "");
                                }),
                     this);

    m_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m_list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(m_list, "navigation-sidebar");

    // One click here, unlike the directory listing: these results are a
    // transient answer to something just typed, not a place to browse.
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(m_list), TRUE);

    g_signal_connect(m_list, "row-activated",
                     G_CALLBACK(+[](GtkListBox *, GtkListBoxRow *row, gpointer data)
                                {
                                    auto *self = static_cast<SearchBar *>(data);
                                    const auto *path = static_cast<const char *>(
                                        g_object_get_data(G_OBJECT(row), kResultPathKey));
                                    if (path != nullptr && self->m_on_activated)
                                        self->m_on_activated(path);
                                }),
                     this);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), m_list);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), kResultsHeight);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);

    m_status = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(m_status), 0.0f);
    gtk_widget_add_css_class(m_status, "dim-label");
    gtk_widget_set_margin_start(m_status, 12);
    gtk_widget_set_margin_end(m_status, 12);
    gtk_widget_set_margin_bottom(m_status, 6);
    gtk_widget_set_visible(m_status, FALSE);

    GtkWidget *results = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(results), scroller);
    gtk_box_append(GTK_BOX(results), m_status);
    gtk_box_append(GTK_BOX(results), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    // The results push the tabs down while they are showing and give the
    // room straight back when the search is cleared.
    m_revealer = gtk_revealer_new();
    gtk_revealer_set_child(GTK_REVEALER(m_revealer), results);
    gtk_revealer_set_transition_type(GTK_REVEALER(m_revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(GTK_REVEALER(m_revealer), FALSE);

    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(m_root), m_entry);
    gtk_box_append(GTK_BOX(m_root), m_revealer);
}

SearchBar::~SearchBar()
{
    // Top of the destructor: the walk reads this object's members, and
    // ~Actor() would join far too late. Retire any walk in flight first so
    // the join does not wait out a whole tree.
    m_generation.fetch_add(1);
    stop();

    // Anything still queued on the GTK thread now becomes a no-op.
    m_alive->store(false);
}

void SearchBar::set_root_provider(std::function<std::string()> fn)
{
    m_root_provider = std::move(fn);
}

void SearchBar::set_on_activated(std::function<void(std::string)> fn)
{
    m_on_activated = std::move(fn);
}

void SearchBar::clear_results()
{
    gtk_list_box_remove_all(GTK_LIST_BOX(m_list));
    gtk_widget_set_visible(m_status, FALSE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(m_revealer), FALSE);
}

// GTK thread: another keystroke has landed.
void SearchBar::on_search_changed()
{
    const char *typed = gtk_editable_get_text(GTK_EDITABLE(m_entry));
    const std::string query = typed != nullptr ? typed : "";

    // Every keystroke retires the search before it.
    const std::uint64_t generation = m_generation.fetch_add(1) + 1;

    if (query.empty())
    {
        clear_results();
        return;
    }

    // The root is read here, on the GTK thread that owns it, and carried
    // into the walk by value.
    const std::string root = m_root_provider ? m_root_provider() : std::string();
    if (root.empty())
        return;

    post([this, root, query, generation] { run_search(root, query, generation); });
}

// GTK thread: Enter in the entry opens the first result.
void SearchBar::on_entry_activated()
{
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(m_list), 0);
    if (first == nullptr || !m_on_activated)
        return;

    const auto *path = static_cast<const char *>(g_object_get_data(G_OBJECT(first), kResultPathKey));
    if (path != nullptr)
        m_on_activated(path);
}

// This widget's own thread: walk the tree, match by name, and hand back
// whatever was found -- unless the typing has already moved on.
void SearchBar::run_search(std::string root, std::string query, std::uint64_t generation)
{
    namespace fs = std::filesystem;

    const std::string needle = lowercase(query);

    std::vector<Match> matches;
    bool truncated = false;
    std::size_t visited = 0;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;

    while (!ec && it != end)
    {
        // Abandoned: the query has changed since this walk started.
        if (m_generation.load() != generation)
            return;

        if (++visited > kMaxVisited)
        {
            truncated = true;
            break;
        }

        const fs::path path = it->path();
        const std::string name = path.filename().string();

        // Hidden files and directories stay out, exactly as they do in the
        // directory listing -- and skipping a hidden directory keeps the
        // walk out of .git and friends entirely.
        if (!name.empty() && name.front() == '.')
        {
            if (it->is_directory(ec))
                it.disable_recursion_pending();
            ec.clear();
            it.increment(ec);
            continue;
        }

        const bool is_directory = it->is_directory(ec);
        ec.clear();

        if (!is_directory && lowercase(name).find(needle) != std::string::npos)
        {
            Match match;
            match.path = path.string();

            const fs::path relative = fs::relative(path, root, ec);
            match.display = ec ? match.path : relative.string();
            ec.clear();

            matches.push_back(std::move(match));

            if (matches.size() >= kMaxMatches)
            {
                truncated = true;
                break;
            }
        }

        it.increment(ec);
    }

    GtkHost::post([this, alive = m_alive, generation, truncated,
                   matches = std::move(matches)]
                  {
                      if (!alive->load() || m_generation.load() != generation)
                          return;

                      show_results(matches, truncated, true);
                  });
}

// GTK thread: put the answer on screen.
void SearchBar::show_results(const std::vector<Match> &matches, bool truncated, bool searched)
{
    gtk_list_box_remove_all(GTK_LIST_BOX(m_list));

    for (const auto &match : matches)
    {
        GtkWidget *label = gtk_label_new(match.display.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_margin_start(label, 6);
        gtk_widget_set_margin_end(label, 6);
        gtk_widget_set_margin_top(label, 2);
        gtk_widget_set_margin_bottom(label, 2);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        g_object_set_data_full(G_OBJECT(row), kResultPathKey,
                               g_strdup(match.path.c_str()), g_free);

        gtk_list_box_append(GTK_LIST_BOX(m_list), row);
    }

    if (matches.empty())
    {
        gtk_label_set_text(GTK_LABEL(m_status), searched ? "no matching files" : "");
        gtk_widget_set_visible(m_status, TRUE);
    }
    else if (truncated)
    {
        gtk_label_set_text(GTK_LABEL(m_status), "showing the first matches found");
        gtk_widget_set_visible(m_status, TRUE);
    }
    else
    {
        gtk_widget_set_visible(m_status, FALSE);
    }

    gtk_revealer_set_reveal_child(GTK_REVEALER(m_revealer), TRUE);
}

} // namespace satellite
