#pragma once

#include <functional>

namespace pz
{

// Runs `work` on a new thread that nobody waits for. libnx has no std::thread::detach (it throws, and the app
// ends), so the threads are kept and joined once finished; any still running when PortZone exits end with it.
void runInBackground(std::function<void()> work);

} // namespace pz
