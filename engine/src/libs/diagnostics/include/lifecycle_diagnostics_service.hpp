#pragma once

namespace storm::diag
{

// The logs' lifetime: initialize() creates the logs folder, the guard it returns flushes every logger and shuts spdlog
// down when the stack unwinds. The exit waits for that flush a bounded time (logged when it is cut).
class LifecycleDiagnosticsService final
{
    class Guard
    {
      public:
        explicit Guard(LifecycleDiagnosticsService &inst) : inst_(inst)
        {
        }

        ~Guard()
        {
            inst_.terminate();
        }

      private:
        LifecycleDiagnosticsService &inst_;
    };

  public:
    ~LifecycleDiagnosticsService();

    [[maybe_unused, nodiscard("This guard shall exist until stack unwind")]] Guard initialize();
    void terminate();

  private:
    bool active_ = false;
};

} // namespace storm::diag
