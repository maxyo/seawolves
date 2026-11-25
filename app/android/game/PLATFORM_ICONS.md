# Android platform icons

Paths and commands in this file are relative to `app/`.

## Launcher icon

The Seawolves launcher icon (a three-masted ship under full sail, heeled and making way, in bone white on a
crimson radial gradient) is original artwork drawn for this project: no game images, logos, trademarks or
third-party icon assets are used, traced or embedded.

Masters: `icon/ic_launcher_foreground.svg` and `icon/ic_launcher_background.svg`, 108-unit adaptive
layers with the artwork inside the central 66-unit safe circle, following the
[Android adaptive icon guidance](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive).
Edit the SVGs, then regenerate the drawables:

```sh
python3 tools/android/app_icon.py
```

It writes `res/drawable/ic_launcher_{foreground,background}.xml` (the adaptive layers; the foreground is
also the API 33+ monochrome layer, where only its alpha counts). Every supported Android version (8.0+)
draws adaptive icons, so there are no bitmap mipmaps. The SVGs stay within a subset that maps one to one
onto VectorDrawable (paths, one radial gradient, one clip path); the script rejects anything else.
`python3 tools/android/app_icon.py --check` (part of the host checks) verifies that the vector drawables
match the SVGs. The game application's `icon` and `roundIcon` reference `@mipmap/ic_launcher` and
`@mipmap/ic_launcher_round` (`res/mipmap-anydpi`, and `res/mipmap-anydpi-v33` with the monochrome layer).

## Notification icons

`res/drawable/ic_notification_{download,pause}.xml` are original download/pause glyphs, entirely white
on transparency so Android can tint them.

## Packaging and checks

`tools/android/build_apk.py` packages `android/game/AndroidManifest.xml` and
compiles `android/game/res` with AAPT2. The script generates
`org.stormengine.port.R` and compiles that class with the application.
Generated classes/resources stay in the build directory.

`tools/android/platform_resources.py` names every permitted source resource
and compiled XML entry. Build and publication reject unexpected resource
files; publication also rejects missing platform artwork. Adding an icon
requires updating that exact list rather than allowing arbitrary `res/` data.

After the existing SDK and SDL inputs have been prepared, run this resource
and Java smoke check from `app/`:

```sh
python3 tools/android/tests/platform_icons.py \
  --android-jar ../engine/build/android-ci/tools/platform-36/android-36/android.jar \
  --aapt2 ../engine/build/android-ci/tools/buildtools-36/android-16/aapt2 \
  --sdl-source ../engine/build/android-ci/sources/SDL
```

With another build workspace (`STORM_CI_WORK`), substitute its paths. The check compiles and links the
resources, verifies their exact payload and compiles the generated `R` with all application and SDL Java sources;
it does not build an APK or access a device.
