#pragma once

#include <functional>
#include <thread>

namespace satellite {

// Hosts the GTK main loop on a thread of its own, so that the process's
// initial thread is free to do nothing (or, later, anything that isn't GTK).
//
// GTK itself is single-threaded: every call into GTK, GDK or GtkSourceView
// has to happen on the thread that owns the main context -- the one started
// here. Widget threads reach the interface through post() / call().
class GtkHost
{
public:
    GtkHost(int argc, char **argv);
    ~GtkHost();

    GtkHost(const GtkHost &) = delete;
    GtkHost &operator=(const GtkHost &) = delete;

    // Spin up the GTK thread and open the window.
    void start();

    // Block until the interface closes; returns the application exit status.
    int join();

    // Run fn on the GTK thread and return immediately. Safe from any thread.
    static void post(std::function<void()> fn);

    // Run fn on the GTK thread and wait for it to finish.
    // Never call this FROM the GTK thread -- it would wait on itself.
    static void call(const std::function<void()> &fn);

private:
    int m_argc;
    char **m_argv;
    int m_status = 0;
    std::thread m_thread;
};

} // namespace satellite
