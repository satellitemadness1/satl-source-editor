#include "session.hpp"

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <utility>

namespace satellite {
namespace {

// A stop, so that a broken filesystem cannot spin the slot search forever.
// Not a limit on how many windows are remembered in practice.
constexpr int kSlotCeiling = 256;

} // namespace

Session::Session()
    : Actor("session")
{
    // Alongside the installed program, which is where this project keeps
    // its own things.
    const char *home = g_get_home_dir();
    if (home != nullptr)
        m_directory = std::string(home) + "/.satl/windows";
}

Session::~Session()
{
    // One last write, stamped with the moment this window closed -- that
    // stamp is what puts it at the front of the queue next time. It is
    // posted rather than written here because the queue may still hold
    // earlier writes, and they must land in order.
    //
    // Nothing of this object has been destroyed yet, so the job may read
    // it; stop() below drains the queue and only then joins.
    post([this]
         {
             if (!m_have_state || m_slot <= 0)
                 return;

             write_state(m_last_state, g_get_real_time());
         });

    stop();

    if (m_lock_fd >= 0)
        close(m_lock_fd); // releases the slot
}

std::string Session::slot_path(int slot, const char *suffix) const
{
    return m_directory + "/window-" + std::to_string(slot) + suffix;
}

gint64 Session::closed_stamp(int slot) const
{
    std::ifstream in(slot_path(slot, ".tabs"));
    if (!in)
        return -1; // never used

    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("closed ", 0) != 0)
            continue;

        try
        {
            return std::stoll(line.substr(7));
        }
        catch (const std::exception &)
        {
            return 0;
        }
    }

    return 0; // used, but never closed cleanly
}

int Session::highest_used_slot() const
{
    int highest = 0;

    GDir *dir = g_dir_open(m_directory.c_str(), 0, nullptr);
    if (dir == nullptr)
        return highest;

    while (const char *name = g_dir_read_name(dir))
    {
        int slot = 0;
        if (std::sscanf(name, "window-%d.tabs", &slot) == 1 && slot > highest)
            highest = slot;
    }

    g_dir_close(dir);
    return highest;
}

bool Session::claim_slot()
{
    if (m_directory.empty())
        return false;

    if (g_mkdir_with_parents(m_directory.c_str(), 0700) != 0)
    {
        g_warning("cannot keep session state in %s", m_directory.c_str());
        return false;
    }

    // Two windows starting at the same instant must not survey the same
    // free slots and both decide on one. This lock makes the survey and
    // the claim a single step; it is held for microseconds.
    const std::string claim_path = m_directory + "/.claim.lock";
    const int claim_fd = ::open(claim_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (claim_fd >= 0)
        ::flock(claim_fd, LOCK_EX);

    const int ceiling = std::min(highest_used_slot() + 1, kSlotCeiling);

    int chosen = 0;
    gint64 chosen_stamp = -2;

    for (int candidate = 1; candidate <= ceiling; ++candidate)
    {
        const std::string lock_path = slot_path(candidate, ".lock");

        const int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd < 0)
            continue;

        // A slot another window is holding is simply not free.
        const bool free_now = ::flock(fd, LOCK_EX | LOCK_NB) == 0;
        if (free_now)
        {
            const gint64 stamp = closed_stamp(candidate);

            // Most recently closed wins. Among slots never used, the lowest
            // number wins, which keeps the numbering tidy.
            if (stamp > chosen_stamp)
            {
                chosen = candidate;
                chosen_stamp = stamp;
            }

            ::flock(fd, LOCK_UN);
        }

        ::close(fd);
    }

    if (chosen == 0)
    {
        if (claim_fd >= 0)
            ::close(claim_fd);
        return false;
    }

    const std::string lock_path = slot_path(chosen, ".lock");
    const int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if (fd < 0 || ::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        if (fd >= 0)
            ::close(fd);
        if (claim_fd >= 0)
            ::close(claim_fd);
        return false;
    }

    m_slot = chosen;
    m_lock_fd = fd;

    if (claim_fd >= 0)
        ::close(claim_fd); // survey over; another window may start now

    return true;
}

Session::State Session::load() const
{
    State state;

    if (m_slot <= 0)
        return state;

    std::ifstream in(slot_path(m_slot, ".tabs"));
    if (!in)
        return state; // a slot nobody has used yet

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line.front() == '#')
            continue;

        const auto space = line.find(' ');
        const std::string word = line.substr(0, space);
        const std::string rest =
            space == std::string::npos ? std::string() : line.substr(space + 1);

        if (word == "untitled")
        {
            state.tabs.push_back(Tab{});
        }
        else if (word == "file" && !rest.empty())
        {
            state.tabs.push_back(Tab{rest});
        }
        else if (word == "current")
        {
            try
            {
                state.current = std::stoi(rest);
            }
            catch (const std::exception &)
            {
                state.current = 0; // a mangled file is not worth a crash
            }
        }
    }

    if (state.current < 0 || state.current >= static_cast<int>(state.tabs.size()))
        state.current = 0;

    return state;
}

// This object's own thread.
void Session::write_state(const State &state, gint64 closed_at) const
{
    std::ostringstream out;

    // gtkmm installs a global C++ locale from the environment, and under a
    // grouping locale a stream writes 1,789,949,570,794,034 for a
    // timestamp -- which reads back as 1, and every window then looks
    // equally recently closed. Numbers in a file format are not prose.
    out.imbue(std::locale::classic());

    out << "# satl-source window " << m_slot << "\n";
    out << "closed " << closed_at << "\n";
    out << "current " << state.current << "\n";

    for (const auto &tab : state.tabs)
    {
        if (tab.path.empty())
            out << "untitled\n";
        else
            out << "file " << tab.path << "\n";
    }

    // Written beside the real file and moved into place, so a window dying
    // mid-write cannot leave half a session behind.
    const std::string final_path = slot_path(m_slot, ".tabs");
    const std::string temporary = final_path + ".new";

    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file)
            return;
        file << out.str();
        if (!file)
            return;
    }

    if (g_rename(temporary.c_str(), final_path.c_str()) != 0)
        g_unlink(temporary.c_str());
}

void Session::save(State state)
{
    if (m_slot <= 0)
        return;

    post([this, state = std::move(state)]
         {
             m_last_state = state;
             m_have_state = true;

             // Zero while the window is open: a window still running has
             // not been closed, so it must not outrank one that has.
             write_state(state, 0);
         });
}

} // namespace satellite
