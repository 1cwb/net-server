#pragma once

#include <csignal>

namespace app_center {

// Cleared by the signal handler; worker loops poll it to shut down cleanly.
extern volatile std::sig_atomic_t g_running;

void install_signal_handlers();

}  // namespace app_center
