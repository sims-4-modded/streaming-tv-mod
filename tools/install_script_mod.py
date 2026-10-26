#!/usr/bin/env python3
# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Install, remove or show the script mod of the YouTube TV in the Mods folder of the game.

Usage: python tools/install_script_mod.py install [--build <folder>] [--mods <Mods folder>]
       python tools/install_script_mod.py uninstall [--mods <Mods folder>]
       python tools/install_script_mod.py status [--build <folder>] [--mods <Mods folder>]
       python tools/install_script_mod.py --self-test

The tool copies only two files, ts4_youtube_tv.ts4script and ts4_youtube_tv.package, into Mods\\ts4_youtube_tv. The
game loads a .ts4script file from Mods or from one folder level below Mods, not deeper.

The Mods folder holds mods that are not ours. The tool never changes or removes another file. It replaces or removes
a file only if the file is ours:

- a .ts4script file is ours if it is a zip with ts4_youtube_tv/__init__.pyc;
- a .package file is ours if it holds the interaction tuning of the mod.

The default Mods folder is Documents\\Electronic Arts\\The Sims 4\\Mods. The tool does not make the Mods folder: the
game makes it.
"""

import argparse
import hashlib
import importlib.util
import os
import pathlib
import shutil
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BUILD = ROOT / "build" / "script_mod"
FOLDER = "ts4_youtube_tv"
SCRIPT_FILE = "ts4_youtube_tv.ts4script"
PACKAGE_FILE = "ts4_youtube_tv.package"
FILES = (SCRIPT_FILE, PACKAGE_FILE)
MARKER_MODULE = "ts4_youtube_tv/__init__.pyc"
class InstallError(Exception):
    """An error that stops the operation. The tool shows the text and returns 1."""


# The game folder is read only. The tool refuses a Mods folder in it.
# The keys that the EA app writes when it installs the game. The value "Install Dir" is the game folder.
GAME_REGISTRY_KEYS = (r"SOFTWARE\Maxis\The Sims 4", r"SOFTWARE\WOW6432Node\Maxis\The Sims 4")


def find_game_folders():
    """Give the game folders: TS4_GAME, else the folder that the EA app wrote in the registry.

    The tool refuses to work if it finds no game folder, because then it cannot keep the game folder read only."""
    value = os.environ.get("TS4_GAME")
    if value:
        return [pathlib.Path(value)]
    try:
        import winreg
    except ImportError:
        winreg = None
    if winreg is not None:
        for key in GAME_REGISTRY_KEYS:
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as handle:
                    folder, _ = winreg.QueryValueEx(handle, "Install Dir")
            except OSError:
                continue
            if folder:
                return [pathlib.Path(folder)]
    raise InstallError("the game folder is not known: the registry has no Install Dir of The Sims 4. "
                       "Set TS4_GAME to the game folder.")


def _build_tool():
    """Load tools/build_script_mod.py for its package reader and the instance id of the tuning."""
    path = pathlib.Path(__file__).resolve().parent / "build_script_mod.py"
    spec = importlib.util.spec_from_file_location("ts4_youtube_tv_build_tool", str(path))
    module = importlib.util.module_from_spec(spec)
    write_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = write_bytecode
    return module


def _documents_folder():
    """Return the Documents folder of the user, also when Windows moved it."""
    if sys.platform == "win32":
        import ctypes
        import ctypes.wintypes
        buffer = ctypes.create_unicode_buffer(ctypes.wintypes.MAX_PATH)
        # CSIDL_PERSONAL is 5: the Documents folder.
        if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buffer) == 0:
            return pathlib.Path(buffer.value)
    return pathlib.Path.home() / "Documents"


def default_mods():
    return _documents_folder() / "Electronic Arts" / "The Sims 4" / "Mods"


def sha256(path):
    digest = hashlib.sha256()
    with open(str(path), "rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def check_mods(mods, game_folders):
    """Refuse a Mods folder that does not exist, or one in the game folder."""
    if not mods.is_dir():
        raise InstallError("the Mods folder {} does not exist; start the game once to make it".format(mods))
    if game_folders is None:
        game_folders = find_game_folders()
    resolved = mods.resolve()
    for game in game_folders:
        game = game.resolve()
        if resolved == game or game in resolved.parents or resolved in game.parents:
            raise InstallError("the Mods folder {} is in the game folder or holds it".format(mods))


def is_our_script(path):
    """Return True if the file is a zip with the package of the mod."""
    try:
        with zipfile.ZipFile(str(path)) as archive:
            return MARKER_MODULE in archive.namelist()
    except (OSError, zipfile.BadZipFile):
        return False


def is_our_package(path):
    """Return True if the package holds the interaction tuning of the mod."""
    try:
        tool = _build_tool()
        instance = tool._fnv().fnv1_64(FOLDER + ":WatchYoutubeVideo")
        resources = tool.read_dbpf(pathlib.Path(path).read_bytes())
        return (tool.TYPE_INTERACTION, 0, instance) in resources
    except Exception:  # noqa: BLE001 - a file that the reader cannot read is not ours.
        return False


def is_ours(path):
    if path.name == SCRIPT_FILE:
        return is_our_script(path)
    if path.name == PACKAGE_FILE:
        return is_our_package(path)
    return False


def state(mods, build=None):
    """Return a list of (file name, state text) for the two files of the mod."""
    folder = mods / FOLDER
    rows = []
    for name in FILES:
        path = folder / name
        if not path.exists():
            rows.append((name, "missing"))
        elif not is_ours(path):
            rows.append((name, "NOT OURS (the tool does not change it)"))
        elif build is not None and (build / name).exists() and sha256(build / name) == sha256(path):
            rows.append((name, "installed, same as the build"))
        else:
            rows.append((name, "installed, different from the build"))
    for name in FILES:
        if (mods / name).exists():
            rows.append((name, "WARNING: a copy is also in the root of Mods; the game can load the mod twice"))
    return rows


def install(build, mods, game_folders=None):
    """Copy the two files of the build into Mods\\ts4_youtube_tv. Return the copied paths."""
    check_mods(mods, game_folders)
    sources = [build / name for name in FILES]
    for source in sources:
        if not source.exists():
            raise InstallError("{} does not exist: run tools/build_script_mod.py first".format(source))
    if not is_our_script(sources[0]) or not is_our_package(sources[1]):
        raise InstallError("the files in {} are not the files of the mod".format(build))
    folder = mods / FOLDER
    if folder.exists() and not folder.is_dir():
        raise InstallError("{} exists and is not a folder".format(folder))
    for name in FILES:
        target = folder / name
        if target.exists() and not is_ours(target):
            raise InstallError("{} exists and is not ours; the tool does not replace it".format(target))
    folder.mkdir(exist_ok=True)
    copied = []
    for source in sources:
        target = folder / source.name
        temporary = folder / (source.name + ".tmp_ts4_youtube_tv")
        shutil.copyfile(str(source), str(temporary))
        os.replace(str(temporary), str(target))
        copied.append(target)
    return copied


def uninstall(mods, game_folders=None):
    """Remove the two files of the mod, and the folder if it is then empty. Return the removed paths."""
    check_mods(mods, game_folders)
    folder = mods / FOLDER
    removed = []
    for name in FILES:
        target = folder / name
        if not target.exists():
            continue
        if not is_ours(target):
            raise InstallError("{} is not ours; the tool does not remove it".format(target))
    for name in FILES:
        target = folder / name
        if target.exists():
            target.unlink()
            removed.append(target)
    if folder.is_dir() and not any(folder.iterdir()):
        folder.rmdir()
    return removed


def self_test():
    """Install, check and remove the mod in a temporary Mods folder."""
    tool = _build_tool()
    with tempfile.TemporaryDirectory(prefix="ts4_youtube_tv_install_test_") as temp:
        temp = pathlib.Path(temp)
        build = temp / "build"
        build.mkdir()
        with zipfile.ZipFile(str(build / SCRIPT_FILE), "w") as archive:
            archive.writestr(MARKER_MODULE, b"\x42\x0d\x0d\x0a")
        (build / PACKAGE_FILE).write_bytes(tool.write_dbpf(tool.package_resources()))
        mods = temp / "Mods"
        try:
            install(build, mods)
            raise AssertionError("install must refuse a Mods folder that does not exist")
        except InstallError:
            pass
        mods.mkdir()
        other = mods / "other_mod.ts4script"
        other.write_bytes(b"not ours")
        neighbour = mods / FOLDER / "notes.txt"

        copied = install(build, mods)
        assert [path.name for path in copied] == list(FILES), copied
        assert all(row[1] == "installed, same as the build" for row in state(mods, build)), state(mods, build)
        install(build, mods)  # A second install replaces our own files.

        neighbour.write_text("the file of the user")
        foreign = mods / FOLDER / PACKAGE_FILE
        foreign.write_bytes(b"DBPF of another mod")
        try:
            install(build, mods)
            raise AssertionError("install must refuse a file that is not ours")
        except InstallError:
            pass
        try:
            uninstall(mods)
            raise AssertionError("uninstall must refuse a file that is not ours")
        except InstallError:
            pass
        assert foreign.read_bytes() == b"DBPF of another mod"
        foreign.unlink()

        removed = uninstall(mods)
        assert [path.name for path in removed] == [SCRIPT_FILE], removed
        assert neighbour.exists(), "uninstall removed a file that is not ours"
        assert (mods / FOLDER).is_dir()
        neighbour.unlink()
        uninstall(mods)
        assert other.read_bytes() == b"not ours"

        install(build, mods)
        uninstall(mods)
        assert not (mods / FOLDER).exists()
        try:
            check_mods(mods, [temp])
            raise AssertionError("the tool must refuse a Mods folder in the game folder")
        except InstallError:
            pass
    print("self-test: OK")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Install the script mod of the YouTube TV.")
    parser.add_argument("command", nargs="?", choices=("install", "uninstall", "status"))
    parser.add_argument("--build", default=str(DEFAULT_BUILD), help="the folder of the built files")
    parser.add_argument("--mods", default=None, help="the Mods folder of the game")
    parser.add_argument("--self-test", action="store_true", help="test the tool in a temporary folder")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.command is None:
        parser.error("give a command, or --self-test")
    mods = pathlib.Path(args.mods) if args.mods else default_mods()
    build = pathlib.Path(args.build)
    try:
        if args.command == "install":
            for path in install(build, mods):
                print("installed {}".format(path))
        elif args.command == "uninstall":
            removed = uninstall(mods)
            for path in removed:
                print("removed {}".format(path))
            if not removed:
                print("the mod is not installed in {}".format(mods / FOLDER))
        else:
            print("Mods folder: {}".format(mods))
            for name, text in state(mods, build):
                print("  {}: {}".format(name, text))
        return 0
    except InstallError as error:
        print("error: {}".format(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
