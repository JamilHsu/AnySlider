#pragma once
#include <atomic>
#include <string>
namespace touchscreen_connect {
// Virtual peer only: opens the other end of a user-configured COM pair.
int RunEloEmulator(const std::string& port, const std::atomic_bool& running);
}
