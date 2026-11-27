#!/usr/bin/env python3
"""The scene sweep never deletes or writes through a symbolic link (tools/scene_sweep/run.py prepare):
a game folder with a linked RESOURCE is refused loudly and the linked folder stays intact; a read-only
game folder is swept from a copy that gets only the sweep's own scripts, without any change to the player's files;
the engine's settings go to the player's folders the engine names (configure)."""

import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import types

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('scene_sweep', root / 'tools/scene_sweep/run.py')
sweep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sweep)


def tree(path):
    return {str(p.relative_to(path)): (p.read_bytes() if p.is_file() else None) for p in sorted(path.rglob('*'))}


def make_game(game, resource=None):
    game.mkdir()
    (game / 'engine.ini').write_text('screen_x = 1\n')
    (game / 'options').write_text('options\n')
    (game / 'PROGRAM').mkdir()
    (game / 'SAVE').mkdir()
    (game / 'SAVE' / 'profile').write_text('save\n')
    if resource:
        (game / 'RESOURCE').symlink_to(resource, target_is_directory=True)
    else:
        (game / 'RESOURCE' / 'techniques').mkdir(parents=True)
        (game / 'RESOURCE' / 'techniques' / 'game.fx').write_text('game\n')
        (game / 'RESOURCE' / 'model.gm').write_text('model\n')  # untouched by prepare: stays a hard link


def shard(game):
    return types.SimpleNamespace(
        arguments=types.SimpleNamespace(
            game=game,
            render='vulkan',
            save=None,
            scenario=None,
            relief_models=None,
            program_cache=None,
            script_cache=None,
        ),
        size=(640, 360),
        queue=['loc:test'],
        context={},
    )


def expect_link_error(call, what):
    try:
        call()
    except sweep.LinkError as error:
        print(f'PASS refused: {what}: {error}'.splitlines()[0][:200])
        return
    raise AssertionError(f'not refused: {what}')


with tempfile.TemporaryDirectory(prefix='sweep-links-') as directory:
    base = Path(directory)
    techniques = base / 'src-techniques'
    techniques.mkdir()
    (techniques / 'engine.fx').write_text('engine\n')

    # inside(): a link anywhere on the way, or a path resolving outside, is refused.
    (base / 'scratch' / 'real').mkdir(parents=True)
    (base / 'scratch' / 'via').symlink_to(base / 'src-techniques', target_is_directory=True)
    assert sweep.inside(base / 'scratch', base / 'scratch' / 'real' / 'file') == base / 'scratch' / 'real' / 'file'
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'scratch' / 'via' / 'engine.fx'), 'a linked parent')
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'scratch' / 'via'), 'the link itself')
    expect_link_error(lambda: sweep.inside(base / 'scratch', base / 'elsewhere'), 'a path outside the copy')

    # chmod: only the copy's directories; a regular file is refused, hard-linked or not.
    (base / 'scratch' / 'plain').write_text('plain\n')
    os.link(base / 'scratch' / 'plain', base / 'scratch' / 'linked')
    expect_link_error(lambda: sweep.make_directory_writable(base / 'scratch' / 'plain'), 'a chmod of a regular file')
    expect_link_error(
        lambda: sweep.make_directory_writable(base / 'scratch' / 'linked'), 'a chmod of a hard-linked file'
    )
    # A write: never into a file with other names (nlink > 1); own_file gives a new inode.
    expect_link_error(
        lambda: sweep.check_own_file(base / 'scratch' / 'linked'), 'a written file shared with another folder'
    )
    fresh = sweep.own_file(base / 'scratch', base / 'scratch' / 'linked')
    fresh.write_text('own\n')
    assert (base / 'scratch' / 'plain').read_text() == 'plain\n' and os.stat(fresh).st_nlink == 1
    sweep.check_own_file(fresh)
    print('PASS a write replaces a hard-linked file with its own inode, the other name unchanged')

    # A game folder whose RESOURCE links to a shared folder: refused, the shared folder untouched.
    shared = base / 'shared-RESOURCE'
    (shared / 'techniques').mkdir(parents=True)
    (shared / 'techniques' / 'game.fx').write_text('shared\n')
    before = tree(shared)
    linked = base / 'linked-game'
    make_game(linked, resource=shared)
    run = base / 'run-1'
    run.mkdir()
    expect_link_error(
        lambda: sweep.Shard.prepare(shard(linked), run), 'a game folder with a linked RESOURCE'
    )
    assert tree(shared) == before, 'the linked shared folder was changed'
    print('PASS the linked shared folder is unchanged')

    # --game given as a link to the real folder: the copy would be the link itself.
    (base / 'game-link').symlink_to(readonly_target := base / 'real-game', target_is_directory=True)
    make_game(readonly_target)
    run = base / 'run-3'
    run.mkdir()
    expect_link_error(
        lambda: sweep.Shard.prepare(shard(base / 'game-link'), run), '--game given as a link'
    )

    # A read-only game folder (a canonical copy kept read-only): prepared from a writable copy, the source unchanged.
    readonly = base / 'readonly-game'
    make_game(readonly)
    before = tree(readonly)
    for path in [readonly, *readonly.rglob('*')]:
        path.chmod(path.stat().st_mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))
    run = base / 'run-2'
    run.mkdir()
    prepared = shard(readonly)
    game, control = sweep.Shard.prepare(prepared, run)
    # The sweep's script is the copy's own inode, never written through a hard link (the read-only source would
    # refuse it with EACCES, a writable one would be changed); the game's own files stay links of the player's.
    assert os.stat(game / 'PROGRAM/scene_sweep.c').st_nlink == 1
    for kept in ('engine.ini', 'options', 'RESOURCE/techniques/game.fx'):
        assert os.stat(game / kept).st_nlink > 1, f'{kept} of the copy was rewritten'
    folders = {name: run / name for name in ('settings', 'saves', 'cache')}
    sweep.Shard.configure(prepared, folders)
    assert 'screen_x = 640' in (folders['settings'] / 'engine.ini').read_text()
    assert (game / 'engine.ini').read_text() == 'screen_x = 1\n'
    assert tree(readonly) == before, 'the read-only game folder was changed'
    assert all(not os.stat(p).st_mode & stat.S_IWUSR for p in [readonly, *readonly.rglob('*')]), (
        'a source mode was changed'
    )
    # A chmod of a copy's file goes through to the player's file: the snapshot (mode included) catches it.
    snapshot = sweep.game_snapshot(readonly)
    (game / 'PROGRAM').chmod(0o755)
    shared = game / 'RESOURCE' / 'model.gm'
    assert os.stat(shared).st_nlink > 1
    os.chmod(shared, os.stat(shared).st_mode | stat.S_IWUSR)
    try:
        sweep.check_game_unchanged(readonly, snapshot, 'test')
    except RuntimeError as error:
        print(f'PASS a chmod through a hard link is reported: {str(error)[:120]}')
    else:
        raise AssertionError('a chmod through a hard link went unnoticed')
    for path in [readonly, *readonly.rglob('*')]:
        path.chmod(path.stat().st_mode | stat.S_IWUSR)
    print(
        'PASS a read-only game folder: swept from a copy holding only the sweep\'s own scripts, its settings in the '
        "player's folders, the source unchanged"
    )
print('PASS scene sweep: no delete or write through a link, read-only game folders')
