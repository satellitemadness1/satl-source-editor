#pragma once

#include "directory_view.hpp"
#include "editor_tabs.hpp"
#include "search_bar.hpp"
#include "session.hpp"

#include <gtkmm.h>

#include <memory>

namespace satellite {

// The shell: gtkmm for the window furniture, the C API for the widgets
// living inside it.
class MainWindow : public Gtk::ApplicationWindow
{
public:
    MainWindow();

private:
    // Collect what is open and hand it to the session store. GTK thread.
    void remember_session();

    // Declared in this order on purpose. Members are destroyed in reverse,
    // so whoever holds a callback into another widget is destroyed first:
    // the search bar calls into both the pane and the tabs, and the pane
    // calls into the tabs.
    // The session store is first, so it is destroyed LAST: its destructor
    // writes the final state, stamped with the moment this window closed,
    // and the tabs it describes must still have existed when it was told.
    std::unique_ptr<Session> m_session;
    std::unique_ptr<EditorTabs> m_tabs;
    std::unique_ptr<DirectoryView> m_directory;
    std::unique_ptr<SearchBar> m_search;
};

} // namespace satellite
