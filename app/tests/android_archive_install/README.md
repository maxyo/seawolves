Run `python3 tests/android_archive_install/run.py` from the repository root. Optionally pass
`--android-jar /absolute/path/android.jar` to also compile the real native wrapper and adapter against the SDK API.
All compiler output and fixtures use a temporary directory.

The checks run the production `ResourceImporter` and `ArchiveInstaller` against a scripted engine reader that returns
documents in the engine's discovery/extraction format (engine `android/archive/README.md`). They do not execute an
Android Activity, JNI, native decoding or networking; the engine's archive tests own format and limit coverage.
