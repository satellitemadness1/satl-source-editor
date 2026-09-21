#pragma once

#include "actor.hpp"

#include <gtk/gtk.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace satellite {

// The search bar across the top of the editing half: type, and every file
// under the directory the pane is showing -- that directory and everything
// inside it, however deep -- is matched by name as the letters arrive.
//
// The walk happens on this widget's own thread, because a deep tree can
// take a long time and the interface may not wait for it. Each keystroke
// starts a new search and retires the one before it by bumping a
// generation; a walk that finds itself out of date abandons its tree
// part-way rather than finishing work nobody wants.
class SearchBar : public Actor
{
public:
    SearchBar();
    ~SearchBar() override;

    // GTK thread only.
    GtkWidget *widget() const { return m_root; }

    // Where to search. Asked on the GTK thread as each search starts, so it
    // always follows wherever the directory pane has been navigated to.
    void set_root_provider(std::function<std::string()> fn);

    // What to do with a result the user picks.
    void set_on_activated(std::function<void(std::string)> fn);

private:
    struct Match
    {
        std::string path;    // absolute, for opening
        std::string display; // relative to the search root, for reading
    };

    // GTK thread.
    void on_search_changed();
    void on_entry_activated();
    void show_results(const std::vector<Match> &matches, bool truncated, bool searched);
    void clear_results();

    // This widget's own thread.
    void run_search(std::string root, std::string query, std::uint64_t generation);

    GtkWidget *m_root = nullptr;     // vertical box: entry over the results
    GtkWidget *m_entry = nullptr;
    GtkWidget *m_revealer = nullptr; // holds the results, hidden when idle
    GtkWidget *m_list = nullptr;
    GtkWidget *m_status = nullptr;   // "no matches", "first 200 of many"

    std::function<std::string()> m_root_provider;
    std::function<void(std::string)> m_on_activated;

    // Bumped on the GTK thread for every keystroke; read by the walk.
    std::atomic<std::uint64_t> m_generation{0};

    // Lets a closure already queued on the GTK thread discover that the bar
    // it was going to fill has since been destroyed.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace satellite
