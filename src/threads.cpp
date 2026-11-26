#include "threads.hpp"

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

namespace pz
{

namespace
{

    struct Running
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    std::mutex runningMutex;
    // Never destroyed: destroying a std::thread that is still running ends the program.
    std::list<Running>& running = *new std::list<Running>();

} // namespace

void runInBackground(std::function<void()> work)
{
    auto finished = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(runningMutex);
    for (auto it = running.begin(); it != running.end();)
    {
        if (*it->finished)
        {
            it->thread.join();
            it = running.erase(it);
        }
        else
            ++it;
    }
    running.push_back({ std::thread([finished, work = std::move(work)] {
                           work();
                           *finished = true;
                       }),
                        finished });
}

} // namespace pz
