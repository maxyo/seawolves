#pragma once
#ifdef __ANDROID__
#include <exception>
#include <spdlog/spdlog.h>
#include <string>

namespace storm::android
{
// Why a reader of the HUD's state from the scripts' data (ship telemetry, battle abilities, gameplay snapshot)
// failed: logged as an error once per change of reason, since the reader runs every frame and the same failure
// would flood the log, while a new reason is new information. The HUD still shows its degraded state.
class HudReadError
{
  public:
    explicit HudReadError(const char *reader) : reader_(reader)
    {
    }
    // In the reader's catch block.
    void report(std::exception_ptr failure, const char *shown)
    {
        std::string reason;
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::exception &error)
        {
            reason = error.what();
        }
        catch (...)
        {
            reason = "an exception that is not a std::exception";
        }
        if (reason == last_)
            return;
        last_ = reason;
        spdlog::error("HUD {}: {}; the HUD shows {} (logged once per reason)", reader_, reason, shown);
    }

  private:
    const char *reader_;
    std::string last_;
};
} // namespace storm::android
#endif
