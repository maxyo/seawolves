Run `python3 app/tests/java_check.py android_archive_install`. All compiler output and fixtures use a temporary
directory.

The checks run the production `ResourceImporter` and `ArchiveInstaller` against a scripted engine reader that returns
documents in the engine's discovery/extraction format (engine `android/archive/README.md`). They do not execute an
Android Activity, JNI, native decoding or networking; the engine's archive tests own format and limit coverage.
