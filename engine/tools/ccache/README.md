# Compiler cache

The native builds (the engine, the UI runtime, the archive and torrent readers, host tools and CMake-based
tests) compile through `tools/ccache/launcher`. When [ccache](https://ccache.dev) is installed, the launcher
runs every compile through it, so a rebuild, or a build of another checkout of the repository, reuses earlier
compiler output. When ccache is not installed, the launcher runs the compiler directly: the build produces the
same result, only rebuilds take longer, and configuring a CMake project prints
`ccache not found: compiling without a compiler cache`.

## How it works

- `cmake/StormBuildCache.cmake`, included right after `project()` by every CMake project of the engine, sets
  `CMAKE_C/CXX_COMPILER_LAUNCHER` to `tools/ccache/launcher`. A different launcher passed with
  `-DCMAKE_<LANG>_COMPILER_LAUNCHER=...` is kept. Build scripts pass the launcher to the third-party projects
  they build, and make-based builds (OpenSSL, FFmpeg) put it in front of `CC`.
- `tools/ccache/launcher` sets these defaults unless they are already set: `CCACHE_DIR=~/.cache/ccache-storm`
  (a cache and a `ccache.conf` of their own, apart from the user's default ccache configuration),
  `CCACHE_BASEDIR` = the directory holding the repository, `CCACHE_NOHASHDIR=1`,
  `CCACHE_COMPILERCHECK=content`, sloppiness `include_file_ctime,include_file_mtime,time_macros,locale`,
  `CCACHE_MAXSIZE=30G` and compression. A `max_size` already written in that cache's `ccache.conf` wins over the
  30G default: several checkouts and Android ABIs built on one machine need more, or rarely built trees lose
  their entries.
- Compile definitions hold no absolute paths (`-DX="/abs/path"`): they would differ between checkouts and miss
  every compile of the target. Text that differs per checkout or commit (the build stamp) goes into a generated
  header that one small file includes (`cmake/build_watermark.cpp`): only that file misses.
- Two checkouts share cache hits when their build directories lie at the same depth: `build/<name>`, or the
  script defaults (`build/android-ci/<name>`).
- Pinned FetchContent sources are fetched once per machine into `~/.cache/storm-deps/fetchcontent/`
  (`storm_fetch_declare`; `STORM_DEPS_CACHE` overrides the root) and shared read-only. Patched ones (bgfx,
  RmlUi) compile from `<build dir>/_deps/<name>-patched-<digest>` (`storm_patched_view`): links into the
  pristine copy plus the patched files; editing a patch reconfigures and rebuilds only its dependents. An
  explicit `FETCHCONTENT_SOURCE_DIR_<NAME>` elsewhere must carry every patch or configuration fails.
- The host `shaderc` is built once per machine into `~/.cache/storm-deps/shaderc-<key>`
  (`src/libs/renderer_bgfx/cmake/storm_shaderc.cmake`; `-DSTORM_SHADERC=<binary>` overrides); compiled shaders
  are cached by content in `~/.cache/storm-deps/shader-cache`.
- Android Release (ThinLTO) links with lld's ThinLTO cache `~/.cache/storm-deps/thinlto-cache`.
- Host builds link with [mold](https://github.com/rui314/mold) when it is installed (`cmake/StormBuildCache.cmake`);
  without it, with the default linker.

## Useful commands

- `tools/ccache/launcher -s` shows the cache's totals (with ccache installed).
- `CCACHE_DISABLE=1` in the environment compiles without the cache, for example to time a full compile.
- `ninja -C <build dir> -n <target> | tail -1` shows how many steps a build would run.

## Scripts that run the compiler directly

The checks that are scripts and call `$CXX` themselves (`storm_script_check` in `tests/host_checks.cmake`) get
`CXX=<repo>/tools/ccache/cxx` from the host check build; so do `tools/android/ci_dependencies.sh` and the gate's
game-data checks. It runs `$STORM_CCACHE_CXX` (default `c++`) through the launcher and splits compile-and-link
commands into cached `-c` compiles plus the link. By hand:
`CXX=tools/ccache/cxx STORM_CCACHE_CXX=clang++ python3 tests/<name>/run.py`. With `STORM_CXX_DEPS=<file>` every
compile appends the files it read; `tools/ci/gate.py` keys its game-data check results by them.
