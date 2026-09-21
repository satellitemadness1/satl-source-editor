#pragma once

#include "actor.hpp"

#include <glib.h>

#include <string>
#include <vector>

namespace satellite {

// What each window had open, remembered between runs.
//
// A window is a process here, so "which window am I?" has to be settled
// between processes. Each one claims a numbered slot by taking an exclusive
// flock and holding it for as long as it lives. The kernel does the
// arbitration, so two windows can never share a slot, and a window that is
// killed outright still gives its slot back -- closing the descriptor is
// what releases the lock.
//
// Which free slot a new window takes is the interesting part: the one
// closed MOST RECENTLY. Close two windows and open two again, and they come
// back in the reverse of the order they were closed in -- the last one you
// closed is the first one you get back. Every state file carries the time
// its window was closed, and a starting window picks the largest.
//
// Tabs the user closes by hand are not remembered: the state is rewritten
// on every change, so a closed tab is simply gone from it.
//
// There is no ceiling on how many windows are remembered.
class Session : public Actor
{
public:
    Session();
    ~Session() override;

    struct Tab
    {
        std::string path; // empty means an untitled document
    };

    struct State
    {
        std::vector<Tab> tabs;
        int current = 0; // index into tabs
    };

    // Take a slot for this process -- the most recently closed one going.
    // False means none could be had, and nothing is remembered this run.
    bool claim_slot();

    // What this slot had open when it was last closed.
    State load() const;

    // Remember this state. Safe from any thread: the write happens on this
    // object's own thread, never on the one drawing the window.
    void save(State state);

    int slot() const { return m_slot; }

private:
    std::string slot_path(int slot, const char *suffix) const;

    // The "closed at" stamp of a slot's state file, or -1 for a slot that
    // has never been used. Zero means it was never closed cleanly.
    gint64 closed_stamp(int slot) const;

    int highest_used_slot() const;

    // This object's own thread.
    void write_state(const State &state, gint64 closed_at) const;

    int m_slot = 0;
    int m_lock_fd = -1; // held open for the life of the process
    std::string m_directory;

    // The actor thread's copy of the last state handed over, so that the
    // final write at shutdown needs no widget to read.
    State m_last_state;
    bool m_have_state = false;
};

} // namespace satellite
