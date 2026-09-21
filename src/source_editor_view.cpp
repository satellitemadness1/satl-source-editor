#include "source_editor_view.hpp"
#include "gtk_host.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>
#include <utility>

namespace satellite {
namespace {

// A ceiling, so that double-clicking a disk image does not take the editor
// (and this view's thread) down with it. 64 MiB.
constexpr std::uintmax_t kMaxFileBytes = 64ull * 1024 * 1024;

// How much is read at a time. Between chunks the read can be abandoned.
constexpr std::size_t kChunkBytes = 64 * 1024;

enum class Load
{
    Ok,
    Failed,    // `notice` says why, and is shown in the buffer
    Abandoned, // the tab was closed; there is nobody left to tell
};

// Reads the whole file into `text`. On failure, `notice` is set to the one
// line to show in the buffer instead.
Load read_text_file(const std::string &path, const std::atomic<bool> &abandon,
                    std::string &text, std::string &notice)
{
    const std::string name = std::filesystem::path(path).filename().string();

    // file_size() is a cheap way to turn away something enormous before
    // opening it. It is NOT the length to read: a file can grow or shrink
    // between the two calls, and anything under /proc reports zero while
    // having plenty to say. The read below goes to EOF and believes only
    // what it actually got.
    std::error_code ec;
    const auto reported = std::filesystem::file_size(path, ec);
    const bool size_known = !ec;

    if (size_known && reported > kMaxFileBytes)
    {
        notice = "(" + name + " is too large to open)";
        return Load::Failed;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        notice = "(" + name + " could not be opened)";
        return Load::Failed;
    }

    std::string data;
    if (size_known)
        data.reserve(static_cast<std::size_t>(reported));

    std::vector<char> chunk(kChunkBytes);

    while (in)
    {
        if (abandon.load())
            return Load::Abandoned;

        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0)
            break;

        if (data.size() + static_cast<std::size_t>(got) > kMaxFileBytes)
        {
            notice = "(" + name + " is too large to open)";
            return Load::Failed;
        }

        data.append(chunk.data(), static_cast<std::size_t>(got));
    }

    // GtkTextBuffer holds UTF-8 and nothing else; handing it bytes that
    // aren't is undefined, not merely ugly. g_utf8_validate with a length
    // also rejects embedded NULs, which is exactly the binary test wanted.
    if (!g_utf8_validate(data.data(), static_cast<gssize>(data.size()), nullptr))
    {
        notice = "(" + name + " is not text)";
        return Load::Failed;
    }

    text = std::move(data);
    return Load::Ok;
}

} // namespace

SourceEditorView::SourceEditorView()
    : Actor("source-editor-view"),
      m_alive(std::make_shared<std::atomic<bool>>(true))
{
    m_view = gtk_source_view_new();
    m_buffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m_view)));

    GtkSourceView *view = GTK_SOURCE_VIEW(m_view);
    gtk_source_view_set_show_line_numbers(view, TRUE);
    gtk_source_view_set_highlight_current_line(view, TRUE);
    gtk_source_view_set_auto_indent(view, TRUE);
    gtk_source_view_set_indent_on_tab(view, TRUE);
    gtk_source_view_set_insert_spaces_instead_of_tabs(view, TRUE);
    gtk_source_view_set_tab_width(view, 4);
    gtk_source_view_set_indent_width(view, 4);
    gtk_source_view_set_smart_backspace(view, TRUE);
    gtk_source_view_set_show_right_margin(view, TRUE);
    gtk_source_view_set_right_margin_position(view, 100);

    gtk_text_view_set_monospace(GTK_TEXT_VIEW(m_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(m_view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(m_view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(m_view), 8);

    m_scroller = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(m_scroller, TRUE);
    gtk_widget_set_vexpand(m_scroller, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_scroller), m_view);
}

SourceEditorView::~SourceEditorView()
{
    // A tab is closed on the GTK thread, so the join below happens there
    // too. Tell a read in flight to give up first, or the whole interface
    // waits for however much of the file is left.
    m_abandon.store(true);

    // Then, at the top of the destructor and before any member is
    // destroyed: this view's thread reads its own members, and ~Actor()
    // would join far too late.
    stop();

    // Anything still queued on the GTK thread now becomes a no-op.
    m_alive->store(false);
}

void SourceEditorView::focus()
{
    gtk_widget_grab_focus(m_view);
}

void SourceEditorView::apply_text(const std::string &text, bool editable)
{
    // Loading a document is not an edit: keep it out of the undo history,
    // so the first ctrl-Z cannot wind the file back to nothing.
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(m_buffer);
    gtk_text_buffer_begin_irreversible_action(buffer);
    gtk_text_buffer_set_text(buffer, text.c_str(), static_cast<int>(text.size()));
    gtk_text_buffer_end_irreversible_action(buffer);

    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(m_view), editable ? TRUE : FALSE);
}

void SourceEditorView::set_text(std::string text)
{
    // Called from anywhere; the buffer is only ever touched on the GTK thread.
    GtkHost::post([this, alive = m_alive, text = std::move(text)]
                  {
                      if (!alive->load())
                          return;

                      apply_text(text, true);
                  });
}

void SourceEditorView::load_file(std::string path)
{
    // The tab is on screen and holding the keyboard from the moment it is
    // created, but the file is still on its way. Hold the buffer read-only
    // until the read lands: apply_text() replaces the whole buffer inside
    // an irreversible action, so anything typed into that gap would be
    // destroyed -- and the undo history cleared along with it, so ctrl-Z
    // could not bring it back either. apply_text() restores editability,
    // or leaves it off for a file that turned out not to be text.
    GtkHost::post([this, alive = m_alive]
                  {
                      if (alive->load())
                          gtk_text_view_set_editable(GTK_TEXT_VIEW(m_view), FALSE);
                  });

    // The read happens on this view's own thread: a slow disk must not be
    // able to stall the interface.
    post([this, alive = m_alive, path = std::move(path)]
         {
             if (m_abandon.load())
                 return; // queued behind a read that has since been closed

             std::string text;
             std::string notice;
             const Load outcome = read_text_file(path, m_abandon, text, notice);

             if (outcome == Load::Abandoned)
                 return; // the tab is gone; there is nothing to fill

             const bool ok = outcome == Load::Ok;

             GtkHost::post([this, alive, ok,
                            body = ok ? std::move(text) : std::move(notice)]
                           {
                               if (!alive->load())
                                   return;

                               apply_text(body, ok);
                           });
         });
}

} // namespace satellite
