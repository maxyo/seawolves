# Security policy

## Supported versions

Fixes go into the latest release only. The project is in beta: there are no maintenance branches.

## Reporting a vulnerability

Please report security problems privately, not in a public issue:

- open a private report at <https://github.com/maxyo/seawolves/security/advisories/new> (the repository's
  **Security** tab → **Report a vulnerability**).

Say what the problem is, how to reproduce it and which version it affects (the About screen shows the version and
the build's commit). You can expect an answer within a week. Once a fix is released the advisory is published
with credit to you, unless you prefer otherwise.

In scope: the app and the engine in this repository. Of particular interest are the parts that read files the
app did not create: the import of a game folder, archive or installer, the download library of the GitHub build
(torrent files and magnet links), the restore of a save archive, and the files the app shares with other apps.

Out of scope: bugs of the game itself that are not a security problem (use [Issues](https://github.com/maxyo/seawolves/issues)),
and problems that need a rooted phone or a debug build.

## Verifying a release

Every release APK is signed with one key. Its certificate's SHA-256 digest is

    01096bac94d9135ed3e45ff23bbe78f002ca09fb5aa7e1a5f16e44810ab09749

`apksigner verify --print-certs <apk>` prints the digest of a downloaded APK; each release also carries
`SHA256SUMS`. An APK with another digest was not built by this project.
