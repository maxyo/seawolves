# script_check

Host tool that compiles a game's script tree without starting the game and reports every compile error with file
and line. It is linked as the engine is (every module), so the script libraries register their natives.

```sh
ninja -C <host build> script_check
cd <game dir>                          # the working directory is the game directory
<build>/script_check check . [--dump <dir>] [--warnings] [--language <name>]
<build>/script_check test [--cache] tests/script_language/cases/*.c
```

`check` compiles the main segment (`seadogs.c`) and then every `.c` file that no script includes, each as its own
segment that is unloaded again, as `LoadSegment` does. A segment that fails on names another segment defines is
compiled again with that segment loaded, since the game loads such segments together. The compiler resumes after an
error, so one run lists everything.
- Natives the scripts declare that no engine library provides are listed as `MISSING NATIVE` lines and in a summary
  line; they are not errors here (an undeclared native fails only when a script calls it).
- The game profile and the `%language%` of include paths are resolved as the engine resolves them.
- `--dump` writes each segment's bytecode (`.b`) and the attribute and function names of its operands (`.aw`), in
  instruction order: the codes depend on the compilation order, so compare two dumps through the names.

`test` runs each script's `int test_*()` functions; each must return 1 without a runtime error, and each
`int fail_*()` must stop with a runtime error. A script whose first line is `// expect-error: <text>` must instead
fail to compile with that error; a line `// profile: Caribbean Legend` compiles it with that game profile. With
`--cache`, a first compiler stores each script in a fresh script cache, and the tests run on a second compiler that
loads it from there. The host checks build it from core's sources alone and run the script language cases both ways
(`tests/script_language`, checks `script_language` and `script_language.cache`).
