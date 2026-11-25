# Character attribute highlight regression

`tools/android/run_host_tests.py` runs it where the game is found (`$STORM_GAME_DIR`) or with `--local`; it
builds `storm_ui_shot` incrementally in `build/ui-shot`. `storm_ui_shot` renders with software GLES, so no GPU is
used. The check taps the real attribute rows of the generated pack through the engine screen router, closes each
description by its button, an outside tap and Back, and checks the live `.tb-row.sel` state.

By hand (the runner does not build the pack):

```sh
python3 mobile-ui/design/build.py --pack <pack> --engine-root <engine-source>
python3 tests/attribute_highlight/run.py \
  --pack <pack> --shot <engine-build>/storm_ui_shot \
  --engine-source <engine-source> --work-dir <work dir>/attribute-check
```
