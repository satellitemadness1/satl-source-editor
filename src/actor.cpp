#include "actor.hpp"

namespace satellite {

Actor::Actor(std::string name)
    : m_name(std::move(name))
{
    m_thread = std::thread([this] { run(); });
}

Actor::~Actor()
{
    stop();
}

void Actor::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();

    if (m_thread.joinable())
        m_thread.join();
}

void Actor::post(std::function<void()> job)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void Actor::run()
{
    for (;;)
    {
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });

            if (m_stopping && m_jobs.empty())
                return;

            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        job();
    }
}

} // namespace satellite
