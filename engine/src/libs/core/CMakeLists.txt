STORM_SETUP(
    TARGET_NAME core
    TYPE library
    DEPENDENCIES diagnostics math shared_headers fast_float ${SDL2_LIBRARIES} window
)
# The file service reads the interface layer's SOURCE.json (platform/ui_overlay.hpp) in every build: the layer's path
# decides whether there is one (the touch build, the UI checks' script VM), not the build option.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/nlohmann_json.cmake")
target_link_libraries(core PRIVATE nlohmann_json::nlohmann_json)

# The VM's checks live in this library only: the other libraries build the same with and without them.
if(STORM_VM_VERIFY)
    target_compile_definitions(core PRIVATE STORM_VM_VERIFY=1)
endif()
