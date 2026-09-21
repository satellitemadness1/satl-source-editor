#include "gtk_host.hpp"

// The process's initial thread starts the GTK thread and then does nothing
// but wait on it. Every widget beyond that carries a thread of its own.
int main(int argc, char **argv)
{
    satellite::GtkHost host(argc, argv);

    host.start();

    return host.join();
}
