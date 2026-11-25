#!/usr/bin/env python3
"""Release versions: the tag, the versionName, the versionCode and the CHANGELOG section of a release.

    release_version.py check --tag v1.0.0-alpha.1 [--rehearse] [--github-output FILE]
    release_version.py code 1.0.0-alpha.1

The declared version is the one line of VERSION at the repository root. A release is built only from the tag
`v<VERSION>` and only when:

- the tag parses (below) and equals `v` + VERSION exactly;
- the tag names HEAD, and the work tree (app/ and engine/) is clean;
- CHANGELOG.md has a non-empty `## <VERSION> — YYYY-MM-DD` section (a rehearsal also accepts `— unreleased`);
- its versionCode is above the code of every other release tag.

`--rehearse` runs the same checks for a tag that does not exist yet (the release workflow's dry run): it never
publishes, so it skips only "the tag names HEAD" and accepts an unreleased CHANGELOG section.

Version syntax (SemVer 2.0, no build metadata; the tag is `v` + the version, the versionName is the version):

    MAJOR.MINOR.PATCH                     1.0.0
    MAJOR.MINOR.PATCH-(alpha|beta|rc)[.N] 1.0.0-alpha.1, 1.0.0-alpha.2, 1.0.0-rc.1

versionCode = MAJOR*10^8 + MINOR*10^6 + PATCH*10^4 + STAGE + N*100, with STAGE alpha 1000, beta 4000, rc 7000
and a final release 9900 (no N). MINOR, PATCH <= 99, N <= 19, MAJOR <= 20 (Android's limit is 2100000000).
A build that is not a release (a local debug build, build_apk.py) is versionName `<VERSION>-dev` with the
versionCode of VERSION: it is that version in development, signed with a local debug key, and installs over an
earlier debug build made with the same key.

    1.0.0-alpha = 100001000   1.0.0-alpha.1 = 100001100   1.0.0-alpha.2 = 100001200
    1.0.0-rc.1  = 100007100   1.0.0      = 100009900   1.0.1-beta   = 100014000   2.0.0 = 200009900
"""
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]  # the repository root: VERSION and CHANGELOG.md
ANDROID_MAX_CODE = 2100000000
STAGES = {'alpha': 1000, 'beta': 4000, 'rc': 7000}
FINAL = 9900
NUMBER = r'(0|[1-9][0-9]*)'
SEMVER = re.compile(rf'{NUMBER}\.{NUMBER}\.{NUMBER}(?:-(alpha|beta|rc)(?:\.{NUMBER})?)?')
TAG_PREFIX = 'v'


class ReleaseError(Exception):
    pass


@dataclass(frozen=True)
class Version:
    text: str
    major: int
    minor: int
    patch: int
    stage: str  # alpha, beta, rc or '' for a final release
    number: int

    @property
    def prerelease(self):
        return bool(self.stage)

    @property
    def code(self):
        return (self.major * 10**8 + self.minor * 10**6 + self.patch * 10**4 +
                (STAGES[self.stage] + self.number * 100 if self.stage else FINAL))

    @property
    def tag(self):
        return TAG_PREFIX + self.text


def parse(text):
    """Parse a SemVer release version; raise ReleaseError on anything else or out of range."""
    match = SEMVER.fullmatch(text)
    if not match:
        raise ReleaseError(f'{text!r} is not a release version (MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-alpha|beta|rc[.N])')
    major, minor, patch, stage, number = match.groups()
    version = Version(text, int(major), int(minor), int(patch), stage or '', int(number or 0))
    if (version.minor > 99 or version.patch > 99 or version.number > 19 or version.major < 1
            or version.code > ANDROID_MAX_CODE):
        raise ReleaseError(f'{text}: out of the versionCode range (MAJOR 1..20, MINOR/PATCH <= 99, N <= 19)')
    return version


def parse_tag(tag):
    if not tag.startswith(TAG_PREFIX):
        raise ReleaseError(f'tag {tag!r} does not start with {TAG_PREFIX!r}')
    return parse(tag[len(TAG_PREFIX):])


def declared(root=ROOT):
    text = (root / 'VERSION').read_text()
    if text != text.strip() + '\n':
        raise ReleaseError('VERSION must hold exactly one line, the version')
    return parse(text.strip())


def changelog_section(text, version, rehearse=False):
    """The body of the `## <version> — <date>` section of CHANGELOG.md (up to the next `## ` heading)."""
    heading = re.compile(rf'^## {re.escape(version)} — (\S+)[ \t]*$', re.M)
    matches = list(heading.finditer(text))
    if len(matches) != 1:
        raise ReleaseError(f'CHANGELOG.md needs exactly one "## {version} — YYYY-MM-DD" section, found {len(matches)}')
    date = matches[0][1]
    if not re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}', date) and not (rehearse and date == 'unreleased'):
        raise ReleaseError(f'CHANGELOG.md section {version} is dated {date!r}: date it (YYYY-MM-DD) before tagging')
    rest = text[matches[0].end():]
    following = re.search(r'^## ', rest, re.M)
    body = (rest[:following.start()] if following else rest).strip()
    if not body:
        raise ReleaseError(f'CHANGELOG.md section {version} is empty')
    return body


def git(*args, root=ROOT):
    return subprocess.run(['git', '-C', str(root), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, check=True).stdout.strip()


def release_tags(root=ROOT):
    """Every tag that parses as a release tag, as Version objects."""
    versions = []
    for tag in git('tag', '--list', TAG_PREFIX + '*', root=root).split():
        try:
            versions.append(parse_tag(tag))
        except ReleaseError:
            continue  # other v* names are not release tags; check() refuses only the one being released
    return versions


def check(tag, rehearse=False, root=ROOT):
    version = parse_tag(tag)
    expected = declared(root)
    if version.text != expected.text:
        raise ReleaseError(f'tag {tag} does not match VERSION {expected.text} (expected tag {expected.tag})')
    dirty = git('status', '--porcelain', root=root)
    if dirty:
        raise ReleaseError('refusing a release from a dirty tree:\n' + dirty)
    if not rehearse:
        try:
            tagged = git('rev-parse', '--verify', '--quiet', f'refs/tags/{tag}^{{commit}}', root=root)
        except subprocess.CalledProcessError:
            raise ReleaseError(f'refusing a release from an untagged commit: tag {tag} does not exist') from None
        if tagged != git('rev-parse', 'HEAD', root=root):
            raise ReleaseError(f'refusing a release: tag {tag} names {tagged}, not HEAD')
    notes = changelog_section((root / 'CHANGELOG.md').read_text(), version.text, rehearse)
    for other in release_tags(root):
        if other.text == version.text:
            continue
        if other.code >= version.code:
            raise ReleaseError(f'versionCode {version.code} of {tag} is not above {other.tag} ({other.code})')
    return version, notes


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = p.add_subparsers(dest='command', required=True)
    c = commands.add_parser('check', help='validate a release tag; print its versionName and versionCode')
    c.add_argument('--tag', required=True)
    c.add_argument('--rehearse', action='store_true', help='dry run: the tag need not exist, nothing is published')
    c.add_argument('--github-output', type=Path, help='append version_name, version_code, prerelease, tag')
    v = commands.add_parser('code', help='print the versionCode of a version')
    v.add_argument('version')
    a = p.parse_args()
    try:
        if a.command == 'code':
            print(parse(a.version).code)
        else:
            version, _ = check(a.tag, a.rehearse)
            values = {'version_name': version.text, 'version_code': version.code,
                      'prerelease': str(version.prerelease).lower(), 'tag': version.tag}
            lines = ''.join(f'{key}={value}\n' for key, value in values.items())
            if a.github_output:
                with a.github_output.open('a') as output:
                    output.write(lines)
            print(lines, end='')
    except ReleaseError as error:
        sys.exit(f'release_version: {error}')


if __name__ == '__main__':
    main()
