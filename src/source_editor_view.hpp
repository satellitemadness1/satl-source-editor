#pragma once

#include "actor.hpp"

#include <gtk/gtk.h>
#include <gtksourceview/gtksource.h>

#include <atomic>
#include <memory>
#include <string>

namespace satellite {

// The editing surface: one of these per tab. GtkSourceView ships no C++
// binding of any kind, so the widget is driven through its C API; the C++
// half is this actor, which carries the view's own thread for work that
// isn't drawing -- reading a file off the disk, above all.
//
// The widget pointers may only be touched on the GTK thread.
class SourceEditorView : public Actor
{
public:
    SourceEditorView();
    ~SourceEditorView() override;

    // The top-level widget to pack into a container. GTK thread only.
    GtkWidget *widget() const { return m_scroller; }

    // Put the keyboard cursor in this view. GTK thread only.
    void focus();

    // Replace the buffer contents. Safe from any thread.
    void set_text(std::string text);

    // Read `path` on this view's own thread, then show it. A file that
    // cannot be read, or isn't text, leaves a one-line notice in its place
    // and the view read-only. Safe from any thread.
    void load_file(std::string path);

private:
    // GTK thread only.
    void apply_text(const std::string &text, bool editable);

    // Set when the view is going away, so a read already under way stops
    // between chunks instead of making the destructor's join wait out the
    // whole file -- on the GTK thread, which is where tabs are closed.
    std::atomic<bool> m_abandon{false};

    GtkWidget *m_scroller = nullptr;
    GtkWidget *m_view = nullptr;
    GtkSourceBuffer *m_buffer = nullptr;

    // Lets a closure already queued on the GTK thread discover that the
    // view it was going to fill has since been closed.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace satellite
