# The game's portable UI host: storm::ui::Host inside the engine process, drawn by the bgfx backend in the frame
# owner's UI views. Built with the touch interface (STORM_TOUCH_UI): Android, and the desktop's development build.
# See README.md.
if(NOT STORM_TOUCH_UI)
    return()
endif()
if(ANDROID)
    set(ui_host_platform storm_ui_runtime android log)
else()
    set(ui_host_platform storm_ui storm_ui_bgfx)
endif()
STORM_SETUP(
    TARGET_NAME ui_host
    TYPE storm_module
    DEPENDENCIES ${ui_host_platform} diagnostics spdlog util sdl
)
