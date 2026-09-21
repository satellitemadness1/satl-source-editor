#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace satellite {

// Every widget in the editor owns one of these: a private thread that does
// that widget's work -- file I/O, parsing, anything that would otherwise
// stall the interface. Nothing here is allowed to touch GTK directly;
// widget work that needs to draw hands a closure to GtkHost::post().
class Actor
{
public:
    explicit Actor(std::string name);
    virtual ~Actor();

    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;

    // Queue a job onto this widget's own thread.
    void post(std::function<void()> job);

    // Drain the queue and join the thread. Idempotent. A subclass whose
    // thread touches its own members MUST call this at the top of its
    // destructor: members are destroyed before ~Actor() gets to join,
    // so waiting until then leaves the thread reading freed memory.
    void stop();

    const std::string &name() const { return m_name; }

private:
    void run();

    std::string m_name;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void()>> m_jobs;
    bool m_stopping = false;
    std::thread m_thread;
};

} // namespace satellite
