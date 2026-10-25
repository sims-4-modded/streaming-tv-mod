#!/usr/bin/env python3
# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Install, remove or show the helper youtube_tv_host.exe and its CEF files for the player.

Usage: python tools/install_host.py install [--build <folder>] [--dest <folder>]
       python tools/install_host.py uninstall [--dest <folder>]
       python tools/install_host.py status [--dest <folder>]
       python tools/install_host.py --self-test

The helper goes into %LOCALAPPDATA%\\Sims4Mods\\youtube_tv, never into the game folder. The tool copies only the files
of youtube_tv_host_files.txt in the build folder (the build writes that list), and it keeps a manifest of these files
with their SHA-256. The tool removes only the files of its manifest that did not change, and the cache of the helper.
It does not replace a file that is not in its manifest.
"""

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BUILD = ROOT / "build" / "x64-release" / "host"
FILE_LIST = "youtube_tv_host_files.txt"
VERSION_FILE = "youtube_tv_host_version.txt"
MANIFEST = "youtube_tv_host_manifest.json"
CACHE_FOLDER = "cache"
# The game folder is read only. The tool refuses a destination in it or a destination that holds it.
GAME_FOLDERS = [pathlib.Path(r"F:\Origin Games\The Sims 4")]


class InstallError(Exception):
    """An error that stops the operation. The tool shows the text and returns 1."""


def default_dest():
    """Give the folder of the helper for the player: %LOCALAPPDATA%\\Sims4Mods\\youtube_tv."""
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        raise InstallError("LOCALAPPDATA is not set")
    return pathlib.Path(local) / "Sims4Mods" / "youtube_tv"


def sha256(path):
    """Give the SHA-256 of a file as hexadecimal text."""
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def check_dest(dest, game_folders):
    """Refuse a destination in the game folder, or one that holds the game folder."""
    resolved = dest.resolve()
    for game in game_folders:
        game = game.resolve()
        if resolved == game or game in resolved.parents or resolved in game.parents:
            raise InstallError(f"the destination {dest} is in the game folder or holds it")


def read_manifest(dest):
    """Give the manifest of an installed helper, or None if there is no manifest."""
    path = dest / MANIFEST
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def read_file_list(build):
    """Give the relative paths of the files that the build made for the helper."""
    list_path = build / FILE_LIST
    if not list_path.exists():
        raise InstallError(f"{list_path} does not exist: build the helper first (tools\\preset.bat x64-release)")
    names = [line.strip() for line in list_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    for name in names:
        relative = pathlib.PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise InstallError(f"the file list has a path that is not relative: {name}")
        if not (build / relative).is_file():
            raise InstallError(f"the build folder does not have {name}")
    return names


def remove_empty_folders(dest):
    """Remove the empty folders in the destination, the deepest first, and the destination if it is empty."""
    for folder in sorted((p for p in dest.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(folder.iterdir()):
            folder.rmdir()
    if dest.exists() and not any(dest.iterdir()):
        dest.rmdir()


def install(build, dest, game_folders=GAME_FOLDERS, log=print):
    """Copy the files of the helper into the destination and write the manifest."""
    check_dest(dest, game_folders)
    names = read_file_list(build)
    old = read_manifest(dest) or {"files": {}}
    old_files = old.get("files", {})

    # Refuse to replace a file that the tool did not install.
    for name in names:
        target = dest / name
        if target.exists() and name not in old_files:
            raise InstallError(f"{target} exists and is not a file of the helper; the tool does not replace it")

    dest.mkdir(parents=True, exist_ok=True)
    files = {}
    copied = 0
    for name in names:
        source = build / name
        target = dest / name
        digest = sha256(source)
        if not target.exists() or sha256(target) != digest:
            target.parent.mkdir(parents=True, exist_ok=True)
            try:
                shutil.copyfile(source, target)
            except PermissionError as error:
                raise InstallError(f"cannot write {target}: stop the helper first ({error})") from error
            copied += 1
        files[name] = digest

    # Remove the files of the old version that the new version does not have.
    removed = 0
    for name, digest in old_files.items():
        target = dest / name
        if name not in files and target.exists() and sha256(target) == digest:
            target.unlink()
            removed += 1

    version = (build / VERSION_FILE).read_text(encoding="utf-8").strip() if (build / VERSION_FILE).exists() else ""
    manifest = {"version": version, "files": files}
    (dest / MANIFEST).write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    remove_empty_folders(dest)
    log(f"installed {len(files)} files in {dest} ({copied} copied, {removed} old files removed)")


def uninstall(dest, game_folders=GAME_FOLDERS, log=print):
    """Remove the files of the manifest that did not change, the cache of the helper and the manifest."""
    check_dest(dest, game_folders)
    manifest = read_manifest(dest)
    if manifest is None:
        log(f"no helper is installed in {dest}")
        return
    kept = []
    removed = 0
    for name, digest in manifest.get("files", {}).items():
        target = dest / name
        if not target.exists():
            continue
        if sha256(target) != digest:
            kept.append(name)
            continue
        try:
            target.unlink()
        except PermissionError as error:
            raise InstallError(f"cannot remove {target}: stop the helper first ({error})") from error
        removed += 1
    # The helper makes its cache folder; it holds no file of the user.
    cache = dest / CACHE_FOLDER
    if cache.is_dir():
        shutil.rmtree(cache)
    (dest / MANIFEST).unlink()
    remove_empty_folders(dest)
    log(f"removed {removed} files from {dest}")
    for name in kept:
        log(f"kept {name}: it changed after the install")


def status(dest, log=print):
    """Show the installed version and the state of each file. Return True if all files are correct."""
    manifest = read_manifest(dest)
    if manifest is None:
        log(f"not installed in {dest}")
        return False
    missing = []
    changed = []
    size = 0
    for name, digest in manifest.get("files", {}).items():
        target = dest / name
        if not target.exists():
            missing.append(name)
        elif sha256(target) != digest:
            changed.append(name)
        else:
            size += target.stat().st_size
    log(f"installed in {dest}: {manifest.get('version', '').replace(chr(10), ', ')}")
    log(f"{len(manifest.get('files', {}))} files, {size / (1 << 20):.1f} MiB, {len(missing)} missing, "
        f"{len(changed)} changed")
    for name in missing:
        log(f"missing: {name}")
    for name in changed:
        log(f"changed: {name}")
    return not missing and not changed


def self_test():
    """Test the tool on a temporary folder with a false build."""
    lines = []
    with tempfile.TemporaryDirectory() as temp:
        temp = pathlib.Path(temp)
        build = temp / "build"
        dest = temp / "dest"
        game = temp / "game"
        (build / "locales").mkdir(parents=True)
        (build / "licenses").mkdir()
        game.mkdir()
        files = {"youtube_tv_host.exe": b"exe", "libcef.dll": b"dll", "locales/en-US.pak": b"pak",
                 "licenses/CEF_LICENSE.txt": b"bsd"}
        for name, data in files.items():
            (build / name).write_bytes(data)
        (build / FILE_LIST).write_text("\n".join(files) + "\n", encoding="utf-8")
        (build / VERSION_FILE).write_text("cef test\n", encoding="utf-8")
        games = [game]

        # The tool refuses the game folder and a folder in it.
        for bad in (game, game / "Mods" / "youtube_tv"):
            try:
                install(build, bad, games, lines.append)
                raise AssertionError("the tool installed into the game folder")
            except InstallError:
                pass

        install(build, dest, games, lines.append)
        assert all((dest / name).read_bytes() == data for name, data in files.items())
        assert status(dest, lines.append)

        # A second install copies nothing and keeps the manifest correct.
        install(build, dest, games, lines.append)
        assert "0 copied" in lines[-1], lines[-1]

        # A new version without a file removes the old file.
        (build / "libcef.dll").unlink()
        (build / FILE_LIST).write_text("\n".join(n for n in files if n != "libcef.dll") + "\n", encoding="utf-8")
        install(build, dest, games, lines.append)
        assert not (dest / "libcef.dll").exists()

        # The tool does not replace a file that it did not install.
        (dest / "libcef.dll").write_bytes(b"of the user")
        (build / "libcef.dll").write_bytes(b"dll")
        (build / FILE_LIST).write_text("\n".join(files) + "\n", encoding="utf-8")
        try:
            install(build, dest, games, lines.append)
            raise AssertionError("the tool replaced a file of the user")
        except InstallError:
            pass
        assert (dest / "libcef.dll").read_bytes() == b"of the user"

        # Uninstall keeps changed files and files of the user, and removes the cache of the helper.
        (dest / "youtube_tv_host.exe").write_bytes(b"changed")
        (dest / CACHE_FOLDER / "Default").mkdir(parents=True)
        (dest / CACHE_FOLDER / "Default" / "data").write_bytes(b"x")
        assert not status(dest, lines.append)
        uninstall(dest, games, lines.append)
        assert (dest / "youtube_tv_host.exe").read_bytes() == b"changed"
        assert (dest / "libcef.dll").read_bytes() == b"of the user"
        assert not (dest / "locales").exists()
        assert not (dest / CACHE_FOLDER).exists()
        assert not (dest / MANIFEST).exists()

        # A file list with a path out of the build folder is refused.
        (build / FILE_LIST).write_text("../outside.txt\n", encoding="utf-8")
        try:
            install(build, temp / "dest2", games, lines.append)
            raise AssertionError("the tool accepted a path out of the build folder")
        except InstallError:
            pass
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", nargs="?", choices=["install", "uninstall", "status"])
    parser.add_argument("--build", type=pathlib.Path, default=DEFAULT_BUILD, help="the folder 'host' of the build")
    parser.add_argument("--dest", type=pathlib.Path, help="the folder of the helper for the player")
    parser.add_argument("--self-test", action="store_true", help="test the tool on a temporary folder")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.action is None:
        parser.error("give an action, or --self-test")
    try:
        dest = args.dest or default_dest()
        if args.action == "install":
            install(args.build, dest)
        elif args.action == "uninstall":
            uninstall(dest)
        else:
            return 0 if status(dest) else 1
    except InstallError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
