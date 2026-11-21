#include "runtime.h"

namespace app_center {

volatile std::sig_atomic_t g_running = 1;

namespace {

void on_signal(int /*sig*/) {
    // Only async-signal-safe work here: flip the flag and let the loop unwind.
    g_running = 0;
}

}  // namespace

void install_signal_handlers() {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    // A peer resetting the connection must not kill the server.
    std::signal(SIGPIPE, SIG_IGN);
}

}  // namespace app_center
