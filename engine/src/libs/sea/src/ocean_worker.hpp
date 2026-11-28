// The thread the sea's wave field is made on while the game thread draws the previous one. README.md.
#pragma once

#include "parallel_for.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace storm::sea::ocean
{
class Worker
{
  public:
    Worker();
    ~Worker();
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;

    // Runs `job` on the worker's thread; the previous job must have been waited for.
    void Start(std::function<void()> job);
    // Returns once the started job has run (at once when none was).
    void Wait();
    // Inside a job: fn(i) for i in [0, count), spread over the worker's helpers (one cascade each).
    template <typename Function> void Parallel(size_t count, Function &&fn)
    {
        helpers_.run(count, std::forward<Function>(fn));
    }

  private:
    void Loop();

    ParallelFor helpers_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::function<void()> job_;
    bool busy_ = false, stop_ = false;
    std::thread thread_;
};
} // namespace storm::sea::ocean
