#include "directory_view.hpp"
#include "gtk_host.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

namespace satellite {

namespace {

// A breath between files, so a directory of ten thousand entries never pins
// a core. 5,000,000 ns is 5 milliseconds -- 0.005 of a second.
constexpr std::chrono::nanoseconds kBetweenFiles{5'000'000};

// ...and a real pause between full passes. This is the "periodically" half:
// without it the scanner would re-walk the listing flat out forever.
constexpr std::chrono::nanoseconds kBetweenPasses =
    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(2));

// How much of a file we look at before deciding whether it has lines at all.
constexpr std::size_t kSampleBytes = 8192;

// Row icon size in pixels.
constexpr int kIconPixelSize = 24;

// The file a row stands for, hung on the row itself: the row-activated
// signal hands back the row and nothing else.
const char *const kRowPathKey = "satl-path";

// Is this sample ASCII or valid UTF-8 -- something with lines in it -- or is
// it bytes we should be reporting in kilobytes instead?
bool sample_is_text(const unsigned char *data, std::size_t length)
{
    for (std::size_t i = 0; i < length;)
    {
        const unsigned char c = data[i];

        if (c == 0x00)
            return false; // a NUL byte settles it

        if (c < 0x80)
        {
            ++i; // plain ASCII
            continue;
        }

        std::size_t continuations;
        if ((c & 0xE0) == 0xC0)
            continuations = 1;
        else if ((c & 0xF0) == 0xE0)
            continuations = 2;
        else if ((c & 0xF8) == 0xF0)
            continuations = 3;
        else
            return false; // stray continuation byte, or an invalid lead

        if (i + continuations >= length)
            return true; // truncated by the edge of the sample; let it pass

        for (std::size_t k = 1; k <= continuations; ++k)
            if ((data[i + k] & 0xC0) != 0x80)
                return false;

        i += continuations + 1;
    }

    return true;
}

// Integer division rounds down, which is exactly the rule: under 1024 bytes
// reads as 0 kb, and 1024 is the first byte count that earns a 1.
std::string format_kilobytes(std::uint64_t bytes)
{
    return "(" + std::to_string(bytes / 1024) + " kb)";
}

std::string format_lines(std::uint64_t lines)
{
    return "(" + std::to_string(lines) + " lines)";
}

// GTK ships no icons of its own -- it resolves freedesktop MIME names
// against whatever icon theme is active. g_content_type_get_icon() hands
// back a themed icon carrying a FALLBACK CHAIN, roughly
//   text-x-python -> text-x-script -> text-x-generic
// and GTK walks that chain until it finds one the theme actually has. Under
// Adwaita, which ships only generic mimetype icons, a .py lands on
// text-x-script and a .cpp on text-x-generic; install a richer theme and the
// per-language icons light up here with no change to this code.
GtkWidget *icon_for(GtkIconTheme *theme, const std::string &name, bool is_directory)
{
    GIcon *icon = nullptr;

    if (is_directory)
    {
        icon = g_themed_icon_new("folder");
    }
    else
    {
        gboolean uncertain = FALSE;
        char *content_type = g_content_type_guess(name.c_str(), nullptr, 0, &uncertain);

        if (content_type != nullptr)
        {
            icon = g_content_type_get_icon(content_type);
            g_free(content_type);
        }

        if (icon == nullptr)
            icon = g_themed_icon_new("text-x-generic");
    }

    // lookup_by_gicon walks the whole fallback chain for us and always
    // returns something, so there is no missing-image case to handle.
    GtkIconPaintable *paintable =
        gtk_icon_theme_lookup_by_gicon(theme, icon, kIconPixelSize, 1,
                                       GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_FORCE_REGULAR);
    g_object_unref(icon);

    GtkWidget *image = gtk_image_new_from_paintable(GDK_PAINTABLE(paintable));
    if (paintable != nullptr)
        g_object_unref(paintable);

    return image;
}

// Turns whatever was typed into the entry into an absolute directory path:
// blank means "stay put", ~ means home, and anything relative resolves
// against the directory currently on screen.
std::string expand_path(const std::string &raw, const std::string &base)
{
    const auto first = raw.find_first_not_of(" \t");
    if (first == std::string::npos)
        return base;

    const auto last = raw.find_last_not_of(" \t");
    std::string text = raw.substr(first, last - first + 1);

    const char *home = g_get_home_dir();
    if (home != nullptr)
    {
        if (text == "~")
            return home;
        if (text.rfind("~/", 0) == 0)
            text = std::string(home) + text.substr(1);
    }

    std::filesystem::path path(text);
    if (path.is_relative() && !base.empty())
        path = std::filesystem::path(base) / path;

    return path.lexically_normal().string();
}

// Counts lines if the file is text. Returns false to mean "report its size
// instead" -- either it is binary, or we could not read it at all.
bool count_lines(const std::string &path, std::uint64_t &lines)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    std::vector<char> buffer(64 * 1024);

    in.read(buffer.data(), static_cast<std::streamsize>(kSampleBytes));
    std::streamsize got = in.gcount();

    if (!sample_is_text(reinterpret_cast<const unsigned char *>(buffer.data()),
                        static_cast<std::size_t>(got)))
        return false;

    std::uint64_t count = 0;
    char last = '\n'; // so that an empty file counts as 0 lines

    while (got > 0)
    {
        for (std::streamsize i = 0; i < got; ++i)
            if (buffer[i] == '\n')
                ++count;

        last = buffer[got - 1];

        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        got = in.gcount();
    }

    if (last != '\n')
        ++count; // a final line with no newline after it is still a line

    lines = count;
    return true;
}

} // namespace

DirectoryView::DirectoryView()
    : Actor("directory-view"),
      m_alive(std::make_shared<std::atomic<bool>>(true))
{
    // Papirus is a declared dependency of this project: it carries real
    // per-language mimetype icons, where Adwaita ships only generic ones.
    //
    // It cannot be selected by renaming the display's icon theme -- GTK
    // refuses to retheme that singleton (is_display_singleton) -- so the
    // pane keeps a private theme and leaves the rest of the desktop alone.
    // Papirus inherits breeze then hicolor, so an icon installed by another
    // program (Satellite's own, for .satl) is still found through it.
    GtkIconTheme *desktop_icons = gtk_icon_theme_get_for_display(gdk_display_get_default());
    GtkIconTheme *papirus = gtk_icon_theme_new();

    char **search_path = gtk_icon_theme_get_search_path(desktop_icons);
    gtk_icon_theme_set_search_path(papirus, search_path);
    g_strfreev(search_path);

    gtk_icon_theme_set_theme_name(papirus, "Papirus");

    if (gtk_icon_theme_has_icon(papirus, "text-x-c++src"))
    {
        m_icons = papirus;
    }
    else
    {
        // Missing on this machine: fall back to the desktop's own theme and
        // show generic icons rather than nothing at all.
        //
        // get_theme_name() is transfer-full, unlike most GTK getters.
        char *fallback_name = gtk_icon_theme_get_theme_name(desktop_icons);
        g_warning("papirus-icon-theme is not installed; file icons fall back to %s",
                  fallback_name);
        g_free(fallback_name);
        g_object_unref(papirus);
        m_icons = GTK_ICON_THEME(g_object_ref(desktop_icons));
    }

    m_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m_list), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(m_list, "navigation-sidebar");

    // FALSE is what makes it a DOUBLE click: with single-click activation
    // off, a first click only selects, and row-activated waits for the
    // second (or for Enter on the selected row).
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(m_list), FALSE);

    g_signal_connect(m_list, "row-activated",
                     G_CALLBACK(+[](GtkListBox *, GtkListBoxRow *row, gpointer data)
                                { static_cast<DirectoryView *>(data)->on_row_activated(row); }),
                     this);

    m_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scroller), m_list);
    gtk_widget_set_vexpand(m_scroller, TRUE);

    m_path_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(m_path_entry), "directory\u2026");
    gtk_widget_set_vexpand(m_path_entry, FALSE);
    gtk_widget_set_margin_start(m_path_entry, 4);
    gtk_widget_set_margin_end(m_path_entry, 4);
    gtk_widget_set_margin_top(m_path_entry, 4);
    gtk_widget_set_margin_bottom(m_path_entry, 4);

    g_signal_connect(m_path_entry, "activate",
                     G_CALLBACK(+[](GtkEntry *, gpointer data)
                                { static_cast<DirectoryView *>(data)->on_path_activated(); }),
                     this);

    // Typing again clears a previous complaint.
    g_signal_connect(m_path_entry, "changed",
                     G_CALLBACK(+[](GtkEditable *editable, gpointer)
                                { gtk_widget_remove_css_class(GTK_WIDGET(editable), "error"); }),
                     this);

    // The entry sits OUTSIDE the scrolled window, so it stays put no matter
    // how long the listing gets.
    m_root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(m_root), m_scroller);
    gtk_box_append(GTK_BOX(m_root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(m_root), m_path_entry);
    gtk_widget_set_size_request(m_root, 160, -1);

    m_scanner = std::thread([this] { scan_loop(); });
}

DirectoryView::~DirectoryView()
{
    // Order matters. Stop both threads while this object's members are still
    // alive -- ~Actor() would join too late, after m_entries had gone.
    {
        std::lock_guard<std::mutex> lock(m_scan_mutex);
        m_scan_stopping = true;
    }
    m_scan_wake.notify_all();

    if (m_scanner.joinable())
        m_scanner.join();

    stop(); // the actor thread

    // Anything still queued on the GTK thread now becomes a no-op.
    m_alive->store(false);

    if (m_icons != nullptr)
        g_object_unref(m_icons);
}

void DirectoryView::open_directory(std::string path)
{
    // The actor thread does the blocking filesystem work.
    post([this, alive = m_alive, path = std::move(path)]
         {
             std::error_code ec;

             const auto resolved = std::filesystem::canonical(path, ec);
             if (ec || !std::filesystem::is_directory(resolved, ec))
             {
                 GtkHost::post([this, alive]
                               {
                                   if (alive->load())
                                       show_path_error();
                               });
                 return;
             }

             std::filesystem::directory_iterator it(resolved, ec);
             if (ec)
             {
                 GtkHost::post([this, alive]
                               {
                                   if (alive->load())
                                       show_path_error();
                               });
                 return;
             }

             std::vector<Entry> entries;

             for (const auto &item : it)
             {
                 Entry entry;
                 entry.name = item.path().filename().string();

                 if (entry.name.empty() || entry.name.front() == '.')
                     continue; // leave dotfiles out for now

                 entry.path = item.path().string();

                 struct stat info{};
                 if (::stat(entry.path.c_str(), &info) != 0)
                     continue;

                 entry.is_directory = S_ISDIR(info.st_mode);

                 // Opening a FIFO blocks until somebody writes to it, and a
                 // device node can block or read forever. The scanner must
                 // never touch one: it would stop measuring for good, and
                 // the destructor's join would then never return.
                 entry.is_special = !entry.is_directory && !S_ISREG(info.st_mode);

                 entry.size = static_cast<std::uint64_t>(info.st_size);
                 entry.mtime = static_cast<std::int64_t>(info.st_mtime);

                 entries.push_back(std::move(entry));
             }

             // Directories first, then by name, case-insensitively.
             std::sort(entries.begin(), entries.end(),
                       [](const Entry &a, const Entry &b)
                       {
                           if (a.is_directory != b.is_directory)
                               return a.is_directory;

                           return std::lexicographical_compare(
                               a.name.begin(), a.name.end(),
                               b.name.begin(), b.name.end(),
                               [](unsigned char x, unsigned char y)
                               { return std::tolower(x) < std::tolower(y); });
                       });

             {
                 std::lock_guard<std::mutex> lock(m_entries_mutex);
                 m_entries = entries;
                 ++m_generation; // the scanner must abandon the old listing
             }

             GtkHost::post([this, alive, directory = resolved.string(),
                            entries = std::move(entries)]
                           {
                               if (alive->load())
                                   rebuild_rows(directory, entries);
                           });

             wake_scanner();
         });
}

void DirectoryView::set_on_file_activated(std::function<void(std::string)> fn)
{
    m_on_file_activated = std::move(fn);
}

// GTK thread: a row was double-clicked (or Enter was pressed on it). Only
// file rows carry a path, so a directory row falls straight through.
void DirectoryView::on_row_activated(GtkListBoxRow *row)
{
    if (row == nullptr || !m_on_file_activated)
        return;

    const auto *path = static_cast<const char *>(g_object_get_data(G_OBJECT(row), kRowPathKey));
    if (path == nullptr)
        return;

    m_on_file_activated(path);
}

// GTK thread: the path entry was committed with Enter.
void DirectoryView::on_path_activated()
{
    const char *typed = gtk_editable_get_text(GTK_EDITABLE(m_path_entry));
    open_directory(expand_path(typed != nullptr ? typed : "", m_shown_directory));
}

// GTK thread: that path was not a directory we could open. Leave the listing
// alone and let the entry say so.
void DirectoryView::show_path_error()
{
    gtk_widget_add_css_class(m_path_entry, "error");
}

// Sleeps for up to `duration`, but cuts it short if there is suddenly work
// to do or the widget is going away. Returns true only for the latter.
//
// Note the predicate covers BOTH flags: a wait_for() whose predicate tests
// only m_scan_stopping would wake on notify, see false, and go straight back
// to sleep for the remainder -- so a freshly opened directory would sit
// unmeasured for the rest of the pause.
bool DirectoryView::nap(std::chrono::nanoseconds duration)
{
    std::unique_lock<std::mutex> lock(m_scan_mutex);

    m_scan_wake.wait_for(lock, duration,
                         [this] { return m_scan_stopping || m_scan_prodded; });

    m_scan_prodded = false;
    return m_scan_stopping;
}

void DirectoryView::wake_scanner()
{
    {
        // Under the mutex: setting the flag outside it races with the
        // scanner deciding to wait, and the wakeup goes missing.
        std::lock_guard<std::mutex> lock(m_scan_mutex);
        m_scan_prodded = true;
    }
    m_scan_wake.notify_all();
}

void DirectoryView::scan_loop()
{
    for (;;)
    {
        std::uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(m_entries_mutex);
            generation = m_generation;
        }

        std::size_t index = 0;
        bool listing_replaced = false;

        for (;;)
        {
            Entry entry;

            {
                std::lock_guard<std::mutex> lock(m_entries_mutex);

                if (m_generation != generation)
                {
                    listing_replaced = true;
                    break;
                }

                if (index >= m_entries.size())
                    break;

                if (m_entries[index].measured || m_entries[index].is_directory ||
                    m_entries[index].is_special)
                {
                    ++index;
                    continue;
                }

                entry = m_entries[index];
            }

            // Measured without the lock held: reading the file is the slow
            // part, and open_directory() must stay free to replace the list.
            std::uint64_t lines = 0;
            const std::string metric = count_lines(entry.path, lines)
                                           ? format_lines(lines)
                                           : format_kilobytes(entry.size);

            publish(entry.path, metric);

            {
                std::lock_guard<std::mutex> lock(m_entries_mutex);
                if (m_generation == generation && index < m_entries.size() &&
                    m_entries[index].path == entry.path)
                    m_entries[index].measured = true;
            }

            ++index;

            if (nap(kBetweenFiles))
                return;
        }

        if (listing_replaced)
            continue; // start a fresh pass over the new listing, right away

        if (nap(kBetweenPasses))
            return;

        restat_entries(generation);
    }
}

// The lazy part: a file whose size and mtime have not moved since we last
// looked is not read again.
void DirectoryView::restat_entries(std::uint64_t generation)
{
    std::vector<std::string> paths;

    {
        std::lock_guard<std::mutex> lock(m_entries_mutex);
        if (m_generation != generation)
            return; // a different directory is on screen now
        paths.reserve(m_entries.size());
        for (const auto &entry : m_entries)
            paths.push_back(entry.path);
    }

    for (const auto &path : paths)
    {
        struct stat info{};
        const bool ok = ::stat(path.c_str(), &info) == 0;

        std::lock_guard<std::mutex> lock(m_entries_mutex);
        if (m_generation != generation)
            return;

        for (auto &entry : m_entries)
        {
            if (entry.path != path)
                continue;

            if (!ok)
            {
                entry.measured = true; // gone, or unreadable; stop asking
                break;
            }

            const auto size = static_cast<std::uint64_t>(info.st_size);
            const auto mtime = static_cast<std::int64_t>(info.st_mtime);

            if (entry.size != size || entry.mtime != mtime)
            {
                entry.size = size;
                entry.mtime = mtime;
                entry.measured = false; // changed underneath us; count again
            }
            break;
        }
    }
}

void DirectoryView::publish(const std::string &path, std::string metric)
{
    GtkHost::post([this, alive = m_alive, path, metric = std::move(metric)]
                  {
                      if (!alive->load())
                          return;

                      const auto row = m_rows.find(path);
                      if (row == m_rows.end())
                          return; // the listing moved on without us

                      gtk_label_set_text(GTK_LABEL(row->second), metric.c_str());
                  });
}

void DirectoryView::rebuild_rows(const std::string &directory,
                                 const std::vector<Entry> &entries)
{
    m_shown_directory = directory;
    gtk_editable_set_text(GTK_EDITABLE(m_path_entry), directory.c_str());
    gtk_widget_remove_css_class(m_path_entry, "error");

    gtk_list_box_remove_all(GTK_LIST_BOX(m_list));
    m_rows.clear();

    for (const auto &entry : entries)
    {
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(line, 6);
        gtk_widget_set_margin_end(line, 6);
        gtk_widget_set_margin_top(line, 2);
        gtk_widget_set_margin_bottom(line, 2);

        GtkWidget *icon = icon_for(m_icons, entry.name, entry.is_directory);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), kIconPixelSize);

        GtkWidget *name = gtk_label_new(entry.name.c_str());
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_hexpand(name, TRUE);

        // Directories say so, and so do the things that are neither file nor
        // directory; a file not yet measured shows an ellipsis.
        const char *initial = entry.is_directory  ? "(dir)"
                              : entry.is_special ? "(special)"
                                                 : "…";
        GtkWidget *metric = gtk_label_new(initial);
        gtk_label_set_xalign(GTK_LABEL(metric), 1.0f);
        gtk_widget_add_css_class(metric, "dim-label");

        gtk_box_append(GTK_BOX(line), icon);
        gtk_box_append(GTK_BOX(line), name);
        gtk_box_append(GTK_BOX(line), metric);

        // The row is built here rather than left to gtk_list_box_append(),
        // so that it can carry the path its double click has to open.
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), line);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row),
                                         (entry.is_directory || entry.is_special) ? FALSE : TRUE);

        // Only openable things carry a path: a double click on a directory
        // or a device node has nothing to open.
        if (!entry.is_directory && !entry.is_special)
            g_object_set_data_full(G_OBJECT(row), kRowPathKey,
                                   g_strdup(entry.path.c_str()), g_free);

        gtk_list_box_append(GTK_LIST_BOX(m_list), row);

        if (!entry.is_directory && !entry.is_special)
            m_rows.emplace(entry.path, metric);
    }

}

} // namespace satellite
