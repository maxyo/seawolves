# Included by every Storm native CMake project (engine, UI runtime, archive and torrent
# readers, host tools, tests). Compiles through tools/ccache/launcher (README.md there), which
# uses ccache when it is installed and runs the compiler directly when it is not. A different
# launcher given with -DCMAKE_<LANG>_COMPILER_LAUNCHER is kept.
# Also provides storm_fetch_declare(): FetchContent_Declare with one shared, read-only
# source copy per pin and machine (STORM_DEPS_CACHE, default ~/.cache/storm-deps), and the readers
# of the pins (engine/pins.json): storm_pin() and storm_fetch_pin().
include_guard(GLOBAL)
include(FetchContent)
get_filename_component(STORM_CCACHE_TOOLS "${CMAKE_CURRENT_LIST_DIR}/../tools/ccache" ABSOLUTE)

# The launcher looks for ccache on PATH when it runs: so does this check, on every configure.
find_program(STORM_CCACHE_PROGRAM ccache NO_CACHE NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)
if(NOT STORM_CCACHE_PROGRAM)
    message(STATUS "ccache not found: compiling without a compiler cache (installing ccache makes rebuilds faster)")
endif()
foreach(lang C CXX)
    # The launcher of this checkout replaces one of another checkout (a build directory configured from elsewhere).
    if(NOT CMAKE_${lang}_COMPILER_LAUNCHER OR CMAKE_${lang}_COMPILER_LAUNCHER MATCHES "/tools/ccache/launcher$")
        set(CMAKE_${lang}_COMPILER_LAUNCHER "${STORM_CCACHE_TOOLS}/launcher" CACHE STRING
            "Set by cmake/StormBuildCache.cmake" FORCE)
    endif()
endforeach()

# Host builds link with mold when it is installed: a host check relinks in a tenth of GNU ld's time, and one edit of
# a library relinks every check that uses it. Without mold the default linker links the same programs.
if(NOT CMAKE_SCRIPT_MODE_FILE AND NOT CMAKE_CROSSCOMPILING)
    find_program(STORM_MOLD_PROGRAM mold NO_CACHE)
    if(STORM_MOLD_PROGRAM)
        add_link_options(-fuse-ld=mold)
    else()
        message(STATUS "mold not found: linking with the default linker (installing mold makes relinks faster)")
    endif()
endif()

if(NOT STORM_DEPS_CACHE)
    if(DEFINED ENV{STORM_DEPS_CACHE})
        set(STORM_DEPS_CACHE "$ENV{STORM_DEPS_CACHE}")
    else()
        set(STORM_DEPS_CACHE "$ENV{HOME}/.cache/storm-deps")
    endif()
endif()
# Runs git outside of any repository in <dir>: the build directory usually lies inside a worktree,
# and git apply there would take paths relative to that repository.
function(storm_git_outside_repo dir result_var output_var)
    # Git looks for a repository in <dir> itself (no .git there) and never above the ceiling. The
    # ceiling is the parent of the normalised <dir>: for "<scratch>/." it is not <scratch> itself, where
    # Git would go on up into the worktree around a build directory and skip every path as outside it.
    cmake_path(SET normal NORMALIZE "${dir}")
    string(REGEX REPLACE "/$" "" normal "${normal}")
    get_filename_component(ceiling "${normal}" DIRECTORY)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${ceiling}" git ${ARGN}
        WORKING_DIRECTORY "${dir}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${output_var} "${output}" PARENT_SCOPE)
endfunction()

# storm_patch_paths(<patch> <paths var> <created var>): the files <patch> changes (relative to the
# directory it applies in) and, of them, the files it creates. Renames and copies are refused.
function(storm_patch_paths patch paths_var created_var)
    set(scratch "${CMAKE_BINARY_DIR}/_deps/storm-patch-paths")
    file(MAKE_DIRECTORY "${scratch}")
    storm_git_outside_repo("${scratch}" result output apply --numstat --summary "${patch}")
    file(REMOVE_RECURSE "${scratch}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot read ${patch}: ${output}")
    endif()
    string(REPLACE "\n" ";" lines "${output}")
    set(paths "")
    set(created "")
    foreach(line ${lines})
        if(line MATCHES "^[-0-9]+\t[-0-9]+\t(.+)$")
            set(path "${CMAKE_MATCH_1}")
            if(path MATCHES " => ")
                message(FATAL_ERROR "${patch} renames or copies ${path}: storm_patched_view supports plain edits, "
                    "creations and deletions only")
            endif()
            list(APPEND paths "${path}")
        elseif(line MATCHES "^ create mode [0-7]+ (.+)$")
            list(APPEND created "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${paths_var} "${paths}" PARENT_SCOPE)
    set(${created_var} "${created}" PARENT_SCOPE)
endfunction()

# storm_patched_view(<pristine> <view> <dir> <patch>...) keeps <view>, a build directory's own view of the
# shared, read-only <pristine> copy with the patches applied in <dir>: a tree of real directories whose
# files are symbolic links into <pristine>, except the files any patch changes, which are real files.
# The view's path does not depend on the patches, so a changed patch keeps every compile command and
# every ccache key of the files it does not touch, and keeps the build incremental:
# - linked files show the pristine copy's timestamps, so Ninja does not rebuild their dependents;
# - a patched file is rewritten only when its content changes, with a fresh timestamp, so Ninja rebuilds
#   exactly its dependents. A file that a patch has ever changed in this view stays a real file
#   (<view>.patched lists them): turning it back into a link would show an old timestamp for a change.
# Patching happens in a scratch copy of only those files; the shared copy is never written.
function(storm_patched_view pristine view patch_dir)
    set(patches ${ARGN})
    set(prefix "${patch_dir}/")
    if(patch_dir STREQUAL ".")
        set(prefix "")
    endif()
    # The link farm, rebuilt when it does not belong to <pristine> (the view's name carries the pin, so
    # this only repairs an interrupted configure).
    set(farm_stamp "")
    if(EXISTS "${view}.farm")
        file(READ "${view}.farm" farm_stamp)
    endif()
    if(NOT IS_DIRECTORY "${view}" OR NOT farm_stamp STREQUAL "${pristine}")
        message(STATUS "Linking the patched view ${view} to ${pristine}")
        file(REMOVE_RECURSE "${view}" "${view}.farm" "${view}.patched")
        file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${pristine}" "${pristine}/*")
        list(FILTER files EXCLUDE REGEX "(^|/)\\.git(/|$)|^\\.storm-complete$")
        set(dirs "${view}")
        foreach(file ${files})
            get_filename_component(dir "${view}/${file}" DIRECTORY)
            list(APPEND dirs "${dir}")
        endforeach()
        list(REMOVE_DUPLICATES dirs)
        file(MAKE_DIRECTORY ${dirs})
        foreach(file ${files})
            file(CREATE_LINK "${pristine}/${file}" "${view}/${file}" SYMBOLIC)
        endforeach()
        file(WRITE "${view}.farm" "${pristine}")
    endif()
    # Every file a current patch changes, plus every file an earlier patch set changed in this view.
    set(patched "")
    if(EXISTS "${view}.patched")
        file(STRINGS "${view}.patched" patched)
    endif()
    foreach(patch ${patches})
        storm_patch_paths("${patch}" paths created)
        foreach(path ${paths})
            list(APPEND patched "${prefix}${path}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES patched)
    set(scratch "${view}.patching")
    file(REMOVE_RECURSE "${scratch}")
    file(MAKE_DIRECTORY "${scratch}/${patch_dir}")
    foreach(path ${patched})
        if(EXISTS "${pristine}/${path}")
            get_filename_component(dir "${scratch}/${path}" DIRECTORY)
            file(MAKE_DIRECTORY "${dir}")
            file(COPY_FILE "${pristine}/${path}" "${scratch}/${path}")
        endif()
    endforeach()
    foreach(patch ${patches})
        storm_git_outside_repo("${scratch}/${patch_dir}" result output apply "${patch}")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Cannot apply ${patch} to ${pristine}/${patch_dir}: ${output}")
        endif()
        # Git skips a path it takes for one outside its work tree and still succeeds: a patch that
        # changed nothing is an error, not a build without the fix.
        storm_patch_paths("${patch}" paths created)
        foreach(path ${paths})
            set(file "${prefix}${path}")
            if(EXISTS "${pristine}/${file}" AND EXISTS "${scratch}/${file}")
                file(SHA256 "${pristine}/${file}" before)
                file(SHA256 "${scratch}/${file}" after)
                if(before STREQUAL after)
                    message(FATAL_ERROR "${patch} applied nothing to ${file} (git: ${output})")
                endif()
            elseif(NOT EXISTS "${pristine}/${file}" AND NOT EXISTS "${scratch}/${file}")
                message(FATAL_ERROR "${patch} did not create ${file} (git: ${output})")
            endif()
        endforeach()
    endforeach()
    foreach(path ${patched})
        set(target "${view}/${path}")
        if(EXISTS "${scratch}/${path}")
            set(changed TRUE)
            if(EXISTS "${target}")
                file(SHA256 "${target}" old)
                file(SHA256 "${scratch}/${path}" new)
                if(old STREQUAL new)
                    set(changed FALSE)
                endif()
            endif()
            if(changed)
                # Remove first: writing through a link would modify the shared copy.
                file(REMOVE "${target}")
                get_filename_component(dir "${target}" DIRECTORY)
                file(MAKE_DIRECTORY "${dir}")
                file(COPY_FILE "${scratch}/${path}" "${target}")
            endif()
        elseif(EXISTS "${target}" OR IS_SYMLINK "${target}")
            file(REMOVE "${target}")
        endif()
    endforeach()
    list(JOIN patched "\n" patched)
    file(WRITE "${view}.patched" "${patched}\n")
    file(REMOVE_RECURSE "${scratch}")
endfunction()

# storm_fetch_declare(<name> <FetchContent_Declare args> [STORM_PATCHES <dir> <patch>...])
# declares <name> with sources fetched once per machine into
# ${STORM_DEPS_CACHE}/fetchcontent/<name>-<digest of the arguments>, a pristine copy shared read-only by
# every build directory of every worktree. With STORM_PATCHES the build uses its own patched view
# ${CMAKE_BINARY_DIR}/_deps/<name>-patched-<same digest> (storm_patched_view; the patches apply in <dir>,
# relative to the source root). Builds never modify shared sources.
# An explicit FETCHCONTENT_SOURCE_DIR_<NAME> still wins.
# STORM_POPULATE_ONLY: FetchContent_MakeAvailable(<name>) only provides the sources (no add_subdirectory);
# the caller adds or compiles what it needs. It does not change the shared copy.
# In script mode (cmake -P) it only provides the sources: FETCHCONTENT_SOURCE_DIR_<NAME>, the view under the working
# directory's _deps with STORM_PATCHES.
function(storm_fetch_declare name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "STORM_POPULATE_ONLY" "" "STORM_PATCHES")
    set(fetch ${arg_UNPARSED_ARGUMENTS})
    string(TOUPPER "${name}" upper)
    # FetchContent keeps FETCHCONTENT_SOURCE_DIR_<NAME> in the cache. A value inside our shared copies or our
    # patched views is the one this function chose at an earlier configure, not an explicit override: it is
    # recomputed, so a changed pin moves a warm build directory to the matching copy.
    set(shared_prefix "${STORM_DEPS_CACHE}/fetchcontent/${name}-")
    set(view_prefix "${CMAKE_BINARY_DIR}/_deps/${name}-patched-")
    string(FIND "${FETCHCONTENT_SOURCE_DIR_${upper}}" "${shared_prefix}" shared_at)
    string(FIND "${FETCHCONTENT_SOURCE_DIR_${upper}}" "${view_prefix}" view_at)
    if(NOT FETCHCONTENT_SOURCE_DIR_${upper} OR shared_at EQUAL 0 OR view_at EQUAL 0)
        string(SHA256 digest "${fetch}")
        string(SUBSTRING "${digest}" 0 16 digest)
        set(pristine "${shared_prefix}${digest}")
        if(NOT EXISTS "${pristine}/.storm-complete")
            file(MAKE_DIRECTORY "${STORM_DEPS_CACHE}/fetchcontent")
            file(LOCK "${pristine}.lock" GUARD FUNCTION TIMEOUT 3600)
            if(NOT EXISTS "${pristine}/.storm-complete")
                message(STATUS "Fetching ${name} once into the shared ${pristine}")
                # A fresh sub-build every time: a kept one believes its clone is up to date even when it was
                # made for another copy, and skips the clone.
                set(subbuild "${CMAKE_BINARY_DIR}/_deps/${name}-shared-subbuild" "${CMAKE_BINARY_DIR}/_deps/${name}-shared-unused")
                file(REMOVE_RECURSE "${pristine}" ${subbuild})
                FetchContent_Populate(${name}_shared QUIET ${fetch}
                    SOURCE_DIR "${pristine}"
                    SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-shared-subbuild"
                    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/${name}-shared-unused")
                file(REMOVE_RECURSE ${subbuild})
                file(WRITE "${pristine}/.storm-complete" "${fetch}\n")
            endif()
        endif()
        set(source "${pristine}")
        if(arg_STORM_PATCHES)
            set(source "${view_prefix}${digest}")
            storm_patched_view("${pristine}" "${source}" ${arg_STORM_PATCHES})
            # Views of earlier pins in this build directory are dead.
            file(GLOB views LIST_DIRECTORIES true "${view_prefix}*")
            list(REMOVE_ITEM views "${source}" "${source}.farm" "${source}.patched")
            if(views)
                file(REMOVE_RECURSE ${views})
            endif()
        endif()
        if(DEFINED CACHE{FETCHCONTENT_SOURCE_DIR_${upper}} AND NOT "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}}" STREQUAL ""
           AND NOT "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}}" STREQUAL "${source}")
            message(WARNING "${name}: the pin or the way it is patched changed; this build directory moves from "
                "$CACHE{FETCHCONTENT_SOURCE_DIR_${upper}} to ${source} (sources recompile once)")
            set(FETCHCONTENT_SOURCE_DIR_${upper} "${source}" CACHE PATH
                "Shared source copy or patched view chosen by storm_fetch_declare (cmake/StormBuildCache.cmake)" FORCE)
        endif()
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${source}" PARENT_SCOPE)
    else()
        # An explicit override (offline sources) must carry the same patches: refuse a tree that lacks one.
        set(patches ${arg_STORM_PATCHES})
        if(patches)
            list(POP_FRONT patches patch_dir)
            foreach(patch ${patches})
                execute_process(COMMAND git apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${FETCHCONTENT_SOURCE_DIR_${upper}}/${patch_dir}"
                    RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
                if(NOT applied EQUAL 0)
                    message(FATAL_ERROR "FETCHCONTENT_SOURCE_DIR_${upper}=${FETCHCONTENT_SOURCE_DIR_${upper}} does not "
                        "carry ${patch}. Drop the override (cmake -U FETCHCONTENT_SOURCE_DIR_${upper} <build dir>: the "
                        "build's own patched view of the shared copy is used) or apply the patch to that tree.")
                endif()
            endforeach()
        endif()
    endif()
    if(CMAKE_SCRIPT_MODE_FILE)
        return() # FetchContent_Declare needs a project
    endif()
    if(arg_STORM_POPULATE_ONLY)
        # A SOURCE_SUBDIR without a CMakeLists.txt: FetchContent_MakeAvailable populates and adds nothing.
        list(APPEND fetch SOURCE_SUBDIR storm-populate-only)
    endif()
    FetchContent_Declare(${name} ${fetch})
endfunction()

# The pinned downloads of the build: engine/pins.json (tools/pins.py reads it for the Python and shell tools). Each
# pin has a version and either an archive (url, sha256) or a Git commit (git, commit); "{version}" and "{commit}" in
# a url stand for the pin's own fields.
get_filename_component(storm_pins_file "${CMAKE_CURRENT_LIST_DIR}/../pins.json" ABSOLUTE)
set(STORM_PINS_FILE "${storm_pins_file}" CACHE INTERNAL "engine/pins.json of this checkout")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${STORM_PINS_FILE}")

# storm_pin(<pin> <field> <var>): a field of a pin, its url expanded; a missing pin or field is an error.
function(storm_pin pin field var)
    file(READ "${STORM_PINS_FILE}" pins)
    string(JSON value ERROR_VARIABLE error GET "${pins}" "${pin}" "${field}")
    if(error)
        message(FATAL_ERROR "${STORM_PINS_FILE}: ${error}")
    endif()
    if(field STREQUAL "url")
        string(JSON version GET "${pins}" "${pin}" version)
        string(REPLACE "{version}" "${version}" value "${value}")
        string(JSON commit ERROR_VARIABLE no_commit GET "${pins}" "${pin}" commit)
        if(NOT no_commit)
            string(REPLACE "{commit}" "${commit}" value "${value}")
        endif()
    endif()
    set(${var} "${value}" PARENT_SCOPE)
endfunction()

# storm_fetch_pin(<name> <pin> [storm_fetch_declare options...]): storm_fetch_declare(<name>) of the pin's archive
# (URL, URL_HASH) or Git commit (GIT_REPOSITORY, GIT_TAG). A macro: storm_fetch_declare sets its result in the
# caller's scope.
macro(storm_fetch_pin storm_fetch_pin_name storm_fetch_pin_pin)
    file(READ "${STORM_PINS_FILE}" storm_fetch_pin_json)
    string(JSON storm_fetch_pin_git ERROR_VARIABLE storm_fetch_pin_no_git GET "${storm_fetch_pin_json}"
        "${storm_fetch_pin_pin}" git)
    if(storm_fetch_pin_no_git)
        storm_pin(${storm_fetch_pin_pin} url storm_fetch_pin_url)
        storm_pin(${storm_fetch_pin_pin} sha256 storm_fetch_pin_sha256)
        storm_fetch_declare(${storm_fetch_pin_name} URL "${storm_fetch_pin_url}"
            URL_HASH "SHA256=${storm_fetch_pin_sha256}" ${ARGN})
    else()
        storm_pin(${storm_fetch_pin_pin} commit storm_fetch_pin_commit)
        storm_fetch_declare(${storm_fetch_pin_name} GIT_REPOSITORY "${storm_fetch_pin_git}"
            GIT_TAG "${storm_fetch_pin_commit}" ${ARGN})
    endif()
endmacro()
