#include "gtk_host.hpp"
#include "main_window.hpp"

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>

#include <future>
#include <utility>

namespace satellite {

GtkHost::GtkHost(int argc, char **argv)
    : m_argc(argc), m_argv(argv)
{
}

GtkHost::~GtkHost()
{
    if (m_thread.joinable())
        m_thread.join();
}

void GtkHost::post(std::function<void()> fn)
{
    // NOT g_main_context_invoke() / Glib::MainContext::invoke(). Those do not
    // always queue: when nobody currently owns the main context, they ACQUIRE
    // it and run the closure INLINE, on the calling thread. Measured with a
    // ten-line glib program -- a worker posting while no loop was running had
    // its closure executed on the worker itself.
    //
    // In this program the loop owns the context for as long as it runs, so
    // the hazard is not at startup; it is at SHUTDOWN. Once the loop returns
    // the context is unowned again, and a widget thread still finishing its
    // work would suddenly be running GTK code against widgets that are being
    // destroyed around it.
    //
    // g_source_attach() only ever queues. The closure runs when the loop
    // dispatches it, on the thread that owns the loop, or never.
    auto *job = new std::function<void()>(std::move(fn));

    GSource *source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        +[](gpointer data) -> gboolean
        {
            (*static_cast<std::function<void()> *>(data))();
            return G_SOURCE_REMOVE; // one shot
        },
        job,
        +[](gpointer data) { delete static_cast<std::function<void()> *>(data); });

    g_source_attach(source, g_main_context_default());
    g_source_unref(source);
}

void GtkHost::call(const std::function<void()> &fn)
{
    std::promise<void> finished;
    auto waiter = finished.get_future();

    post([&fn, &finished]
         {
             fn();
             finished.set_value();
         });

    waiter.wait();
}

void GtkHost::start()
{
    m_thread = std::thread(
        [this]
        {
            // GtkSourceView 5 has to be initialised before its widgets or
            // language definitions can be used.
            gtk_source_init();

            // NON_UNIQUE is what makes "NEW WINDOW" possible. A
            // GApplication carrying an id registers that name on the
            // session bus and every later copy of the program hands its
            // activation to the first one and exits -- measured at 42 ms,
            // with no second window drawn. Non-unique, each copy is its
            // own application and draws its own window.
            auto app = Gtk::Application::create("foundation.satellite.SourceEditor",
                                                Gio::Application::Flags::NON_UNIQUE);
            m_status = app->make_window_and_run<MainWindow>(m_argc, m_argv);

            gtk_source_finalize();
        });
}

int GtkHost::join()
{
    if (m_thread.joinable())
        m_thread.join();

    return m_status;
}

} // namespace satellite
