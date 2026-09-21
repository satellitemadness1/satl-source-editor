#pragma once

#include "actor.hpp"

#include <gtk/gtk.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace satellite {

// The directory pane down the left of the window: a scrolling listing with a
// path entry pinned along the bottom.
//
// Three threads meet here, and each owns exactly one thing:
//
//   * the actor thread (from Actor) enumerates directories -- readdir and
//     stat, the part that blocks on the filesystem;
//   * the scanner sub-thread walks that listing and measures each file,
//     counting lines for text and falling back to kilobytes otherwise;
//   * the GTK thread owns every widget pointer in here, and is the only
//     thread that ever touches one.
//
// Measurements cross from the scanner to the GTK thread as closures, via
// GtkHost::post(). Nothing else crosses.
class DirectoryView : public Actor
{
public:
    DirectoryView();
    ~DirectoryView() override;

    // GTK thread only.
    GtkWidget *widget() const { return m_root; }

    // What the listing is showing right now. GTK thread only.
    const std::string &shown_directory() const { return m_shown_directory; }

    // Safe from any thread.
    void open_directory(std::string path);

    // Called on the GTK thread when a file row is activated -- a double
    // click, or Enter on the selected row. Directory rows never activate.
    // Set it before the pane goes on screen; GTK thread only.
    void set_on_file_activated(std::function<void(std::string)> fn);

private:
    struct Entry
    {
        std::string path;
        std::string name;
        bool is_directory = false;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        bool measured = false;   // cleared again when size or mtime moves
        bool is_special = false; // FIFO, socket, device: never open it
    };

    void scan_loop();                                 // scanner sub-thread
    void restat_entries(std::uint64_t generation);    // scanner sub-thread
    bool nap(std::chrono::nanoseconds duration);      // true => time to quit
    void wake_scanner();                              // there is work now

    // Hand a finished measurement to the GTK thread.
    void publish(const std::string &path, std::string metric);

    // GTK thread only.
    void rebuild_rows(const std::string &directory, const std::vector<Entry> &entries);
    void show_path_error();
    void on_path_activated();
    void on_row_activated(GtkListBoxRow *row);

    // Private icon theme (Papirus), so the pane gets per-language icons
    // without reskinning the rest of the desktop.
    GtkIconTheme *m_icons = nullptr;

    GtkWidget *m_root = nullptr;        // vertical box: scroller over entry
    GtkWidget *m_scroller = nullptr;
    GtkWidget *m_list = nullptr;
    GtkWidget *m_path_entry = nullptr;  // outside the scroller, so always visible

    // GTK thread only.
    std::map<std::string, GtkWidget *> m_rows; // path -> its measurement label
    std::string m_shown_directory;             // what relative paths resolve against
    std::function<void(std::string)> m_on_file_activated;

    // Shared between the actor thread and the scanner. Plain data, no widgets.
    std::mutex m_entries_mutex;
    std::vector<Entry> m_entries;
    // Bumped every time the listing is replaced, so a scanner part-way
    // through a pass over the old listing knows to abandon it.
    std::uint64_t m_generation = 0;

    // Lets a closure already queued on the GTK thread discover that the
    // widget it was going to update has since been destroyed.
    std::shared_ptr<std::atomic<bool>> m_alive;

    std::mutex m_scan_mutex;
    std::condition_variable m_scan_wake;
    bool m_scan_stopping = false;
    // Set when a new listing arrives, so a scanner part-way through its
    // two-second pause between passes stops pausing and gets on with it.
    bool m_scan_prodded = false;
    std::thread m_scanner;
};

} // namespace satellite
