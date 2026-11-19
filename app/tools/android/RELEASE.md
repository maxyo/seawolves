# Releases

A release is built and published by the release workflow (`.github/workflows/release.yml`) from a Git tag, and
only from a tag. Commands here run from the repository root.

## Version, tag and versionCode

`VERSION` (repository root, one line) declares the version being prepared; `CHANGELOG.md` has its section.
Versions are SemVer 2.0 without build metadata; the release tag is `v` + VERSION, exactly, and the versionName is
the version (`app/tools/android/release_version.py` accepts nothing else, e.g. `1.0.0b` is refused):

| | Tag | versionName |
| --- | --- | --- |
| first alpha | `v1.0.0-alpha.1` | `1.0.0-alpha.1` |
| next alphas | `v1.0.0-alpha.2` | `1.0.0-alpha.2` |
| release candidate | `v1.0.0-rc.1` | `1.0.0-rc.1` |
| release | `v1.0.0` | `1.0.0` |

**versionCode** = MAJOR×10⁸ + MINOR×10⁶ + PATCH×10⁴ + stage + N×100, with stage alpha 1000, beta 4000, rc 7000 and
9900 for a final release (no N); MINOR and PATCH up to 99, N up to 19, MAJOR up to 20 (Android allows 2100000000).
It grows with SemVer precedence.

| Version | versionCode |
| --- | --- |
| 1.0.0-alpha | 100001000 |
| 1.0.0-alpha.1 | 100001100 |
| 1.0.0-alpha.2 | 100001200 |
| 1.0.0-rc.1 | 100007100 |
| 1.0.0 | 100009900 |
| 1.0.1-beta | 100014000 |
| 2.0.0 | 200009900 |

`python3 app/tools/android/release_version.py code 1.0.0-alpha.1` prints the code of a version.

A build that is not a release (a local debug build) has the versionName `<VERSION>-dev` and the versionCode of
VERSION. It is signed with a debug key generated on the machine that builds it, so it never updates a released
APK in place and a released APK never updates it: Android refuses an update signed with another key.

## Making a release

1. **Prepare on `main`.** VERSION holds the version; its CHANGELOG section is written for players (Russian and
   English) and dated: `## 1.0.0-alpha.1 — 2026-11-20` (the draft says `— unreleased`, which a tag refuses). Merge
   it and wait for CI.
2. **Rehearse that commit** (optional, recommended; nothing is published):
   `gh workflow run release.yml --ref main -f rehearse_tag=v1.0.0-alpha.1`. The run makes exactly the release build
   with the tag's versionName and versionCode and the release key, runs every publication check, and uploads
   `android-release-draft` (the versioned APK, SHA256SUMS and the release notes it would publish) next to
   `android-release-apk` and `android-native-debug-symbols`.
3. **Tag and push** the same commit:

   ```sh
   git tag -a v1.0.0-alpha.1 -m "Seawolves 1.0.0-alpha.1" <commit>
   git push origin v1.0.0-alpha.1
   ```

   The push starts the workflow. It refuses a tag that does not match VERSION, a tag not on the checked-out
   commit, a dirty tree, an undated or empty CHANGELOG section, a versionCode not above every earlier release,
   and a tagged commit that is not on `main` (the published source of a release is a commit of the main branch).
   It builds the APK from the Release (LTO) engine in `engine/`, signs it with the release key and checks its
   signer.
4. **Publish** (the `publish` job of the same run; it is the only job that can write to the repository and it
   builds nothing). `app/tools/android/publish_release.py` checks the APK again (checksum, build manifest:
   the tag's commit, clean sources, release build, the tag's versionName/versionCode; packaged build-info) and
   that its only signer is the release key (`release_signing.py`, `MAIN_RELEASE_CERT_SHA256`), then creates the
   GitHub release of the tag: `Seawolves <version>`, a pre-release for alpha, beta and rc (not marked latest),
   with `seawolves-<version>-arm64.apk`, `SHA256SUMS` and notes made of the CHANGELOG section plus the APK
   checksum, the certificate digest and a link to the exact source commit. It downloads the APK back and compares
   it. An existing release is never replaced.
5. **After the release**, bump VERSION to the next planned version and open its section as `— unreleased`.

Keep the run's `android-native-debug-symbols` (90 days) for crash
symbolication: a player's report names each native frame's build id, and
`python3 app/tools/android/symbolize.py report.txt --symbols native-debug-symbols.zip` resolves it.

If a tag run fails before the release exists, fix the cause on `main`, delete the tag
(`git push --delete origin v1.0.0-alpha.1`, `git tag -d v1.0.0-alpha.1`) and tag the fixed commit. Once a release
exists its tag is never moved or reused: the fix ships as the next version (`1.0.0-alpha.2`).

## The release key

The workflow reads the key from the secrets `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEY_ALIAS`,
`ANDROID_STORE_PASSWORD` and `ANDROID_KEY_PASSWORD` of the `release` environment; the key exists on the runner
only as a file for the packaging step. The SHA-256 digest of its certificate is public
(`MAIN_RELEASE_CERT_SHA256` in `app/tools/android/release_signing.py`, repeated in every release's notes), so
anyone can check that a downloaded APK is ours:

```sh
apksigner verify --print-certs seawolves-<version>-arm64.apk
```

A fork that publishes its own builds uses its own key: change the digest and the application id
(`build_apk.py --application-id`), since builds signed with different keys cannot share one.

A local release build (`STORM_ANDROID_BUILD_TYPE=release bash app/tools/android/build_debug.sh`) needs
`STORM_RELEASE_TAG` and the four `STORM_ANDROID_*` signing variables and passes the same checks.
`python3 app/tests/android_release_artifact/run.py` (part of the host checks) tests the version scheme, the guards
and the publication against a throwaway repository with a fake tag, `gh` and `apksigner`.

## Boot smoke before a release

Before tagging, start the build on an emulator or a phone. A debug build with
`STORM_ANDROID_TEST_LOOP_SIBLING=1` also makes the `--test-loop` APK from the same native libraries, and

```sh
python3 app/tools/android/emu_smoke.py --apk app/build/apk/testloop/seawolves-arm64-debug.apk \
    --serial <adb serial> --obb <main.*.obb> --out <dir outside the repository>
```

installs it, starts at the main menu, loads the town save of your game subset, checks a frame and the device log
([TESTING.md](../../android/game/TESTING.md)). `--same-libraries-as <APK>` also proves that another APK packs the
same native libraries.
