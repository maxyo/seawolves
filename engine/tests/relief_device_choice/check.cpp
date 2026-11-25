// The device of a relief texture (device_choice.hpp) and the place's loading window (loading_retry.hpp).
#include "device_choice.hpp"
#include "loading_retry.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include <catch2/catch.hpp>
using namespace storm::relief;
TEST_CASE("relief_device_choice")
{
    int windows = 0;
    const auto window = [&](bool grant) {
        return [&windows, grant] {
            ++windows;
            return grant;
        };
    };
    const auto choose = [&](bool possible, bool shown, bool free, bool grant) {
        return ChooseTextureDevice(possible, shown, free, window(grant));
    };
    // The game is played: the CPU, whatever else holds; the loading window is not even asked.
    DeviceChoice c = choose(true, true, true, true);
    REQUIRE((!c.gpu && c.why == DeviceReason::SceneShown &&
             windows == 0)); // the scene shown: CPU, even with a free GPU and a window
    c = choose(true, true, false, false);
    REQUIRE((!c.gpu && c.why == DeviceReason::SceneShown)); // the scene shown: CPU
    // A loading or an interface screen without a scene, the game in the background.
    c = choose(true, false, true, false);
    REQUIRE((c.gpu && c.why == DeviceReason::GpuFree && windows == 0)); // the GPU free: GPU
    // The scene drawn under the loading cover: the window decides.
    c = choose(true, false, false, true);
    REQUIRE(
        (c.gpu && c.why == DeviceReason::LoadingWindow && windows == 1)); // under the loading cover: GPU in the window
    c = choose(true, false, false, false);
    REQUIRE((!c.gpu && c.why == DeviceReason::GameBusy && windows == 2)); // under the loading cover, no window: CPU
    // No usable GPU: never.
    c = choose(false, false, true, true);
    REQUIRE((!c.gpu && c.why == DeviceReason::NoGpu)); // no usable GPU: CPU
    for (DeviceReason why : {DeviceReason::None, DeviceReason::NoGpu, DeviceReason::SceneShown, DeviceReason::GameBusy,
                             DeviceReason::GpuFree, DeviceReason::LoadingWindow})
        REQUIRE(DeviceReasonText(why)[0] != '?'); // every reason has its log text
    // The loading window: one texture, scoped, at most one a minute.
    LoadingRetry lease(500);
    REQUIRE(lease.take(100, true, 0));  // the window grants one texture
    REQUIRE(!lease.take(101, true, 0)); // only one texture consumes the window
    LoadingRetry expired(100);
    REQUIRE(!expired.take(100, true, 0)); // deadline refuses a late texture
    LoadingRetry revoked(500);
    revoked.active = false;
    REQUIRE(!revoked.take(100, true, 0)); // revoked window grants nothing
    LoadingRetry cooldown(60500);
    REQUIRE(!cooldown.take(60000, true, 100)); // one window texture a minute
    REQUIRE(cooldown.take(60100, true, 100));  // the next minute's window grants again
    LoadingRetry missing(500);
    REQUIRE(!missing.take(100, false, 0)); // no usable GPU: the window grants nothing
    std::puts("relief device choice: scene shown CPU, GPU free/loading window GPU: PASS");
}
