#pragma once

#include "actor.hpp"
#include "source_editor_view.hpp"

#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace satellite {

// The tab strip and the documents under it.
//
// This whole widget is the paned's end child, so the tabs span the editing
// half of the window and stop where the directory pane begins -- the
// directory listing is never under a tab.
//
// Like every other widget here it owns a thread: the actor resolves and
// stats a path before anything is put on screen, so a double-click on a
// slow filesystem cannot stall the interface. The notebook, the pages and
// m_tabs belong to the GTK thread alone.
class EditorTabs : public Actor
{
public:
    EditorTabs();
    ~EditorTabs() override;

    // GTK thread only.
    GtkWidget *widget() const { return m_notebook; }

    // Show `path` in a tab. A file that is already open is raised rather
    // than opened twice. Safe from any thread.
    void open_file(std::string path);

    // One tab holding the built-in welcome program, for a window with no
    // remembered session. GTK thread only.
    void open_welcome();

    // What is open, in the order the tabs are shown -- which is the order
    // they are put back in. GTK thread only.
    struct OpenTab
    {
        std::string path; // empty means an untitled document
    };

    std::vector<OpenTab> open_tabs() const;
    int current_index() const;

    // Put a remembered session back. Files that have since been deleted
    // are dropped. Returns false if that left nothing to show, in which
    // case the caller decides what an empty window contains. GTK thread only.
    bool restore(const std::vector<OpenTab> &tabs, int current);

    // Called after anything changes what is open: a tab added, closed, or
    // switched to. GTK thread only.
    void set_on_changed(std::function<void()> fn);

    // A fresh, empty "UntitledN.txt". GTK thread only.
    void open_untitled(const std::string &initial_text = std::string(), bool take_focus = true);

    // Put the keyboard cursor in the document on show. The shell calls this
    // once the notebook is inside a window -- grab_focus() does nothing
    // before that, so the first tab cannot focus itself. GTK thread only.
    void focus_current();

    // What the NEW WINDOW button does. The tabs know nothing about windows,
    // so the shell supplies this. Set it before the pane goes on screen;
    // GTK thread only.
    void set_on_new_window(std::function<void()> fn);

private:
    struct Tab
    {
        std::unique_ptr<SourceEditorView> view;
        GtkWidget *child = nullptr; // the page: the view's own widget
        GtkWidget *label = nullptr; // the name shown on the tab
        std::string path;           // empty while the document is untitled
        std::string title;          // what the tab says
    };

    // All GTK thread only.
    Tab *find_by_path(const std::string &path);
    void add_tab(std::string path, std::string title, const std::string &text, bool take_focus);
    void close_tab(GtkWidget *child);
    void raise(const Tab &tab);
    std::string next_untitled_name() const;
    GtkWidget *build_tab_label(Tab &tab);

    void notify_changed();

    GtkWidget *m_notebook = nullptr;
    std::function<void()> m_on_new_window;
    std::function<void()> m_on_changed;

    // Set while a remembered session is being put back, so that building
    // it does not report itself as a change and rewrite what it is reading.
    bool m_restoring = false;

    // GTK thread only. Page order is the notebook's business; this is just
    // the set of open documents, in no particular order.
    std::vector<std::unique_ptr<Tab>> m_tabs;

    // Lets a closure already queued on the GTK thread discover that the
    // notebook it was going to touch has since been destroyed.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace satellite
