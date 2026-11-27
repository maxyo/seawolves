# nlohmann/json (pin nlohmann-json), the engine's one JSON library: declared here for every project and library that
# uses it (the UI, scene_data, the host checks, the audio import tool). A second inclusion makes the same target
# available and sets storm_json_SOURCE_DIR in the including scope.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
storm_fetch_pin(storm_json nlohmann-json)
FetchContent_MakeAvailable(storm_json)
