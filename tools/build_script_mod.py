#!/usr/bin/env python3
# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Build the script mod of the YouTube TV: ts4_youtube_tv.ts4script and ts4_youtube_tv.package.

Usage: python tools/build_script_mod.py [--out <folder>] [--python <python 3.7>]
       python tools/build_script_mod.py --self-test

The tool compiles each source file of python/ts4_youtube_tv with Python 3.7, because the game runs
Python 3.7 and loads only its bytecode. The .ts4script file is a zip of the .pyc files, with the folder
ts4_youtube_tv at the root of the zip.

The .package file is a DBPF 2.1 package with uncompressed resources: each tuning XML of the folder tuning, and one
string table for each language from tuning/strings.json. The package has no resource of EA.

The tool writes into build/script_mod. It does not install the files: tools/install_script_mod.py does that.
"""

import argparse
import importlib.util
import io
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "python"
PACKAGE_NAME = "ts4_youtube_tv"
TUNING = ROOT / "tuning"
DEFAULT_OUT = ROOT / "build" / "script_mod"

SCRIPT_FILE = PACKAGE_NAME + ".ts4script"
PACKAGE_FILE = PACKAGE_NAME + ".package"

# The first four bytes of a .pyc file of Python 3.7.
PYC_MAGIC_37 = bytes((0x42, 0x0D, 0x0D, 0x0A))

# The resource types of the package.
TYPE_INTERACTION = 0xE882D22F
TYPE_STRING_TABLE = 0x220557DA
TUNING_TYPES = {"interaction": TYPE_INTERACTION}

# The group of the string tables of mods. The game tuning uses group 0.
STRING_TABLE_GROUP = 0x80000000
STRING_TABLE_NAME = PACKAGE_NAME + ":strings"

# The language codes of the game in the high byte of the instance of a string table. The mod has only English text,
# so each language gets the English text. A code that the game does not use does nothing.
LANGUAGE_CODES = {
    "ENG_US": 0x00, "CHS_CN": 0x01, "CHT_CN": 0x02, "CZE_CZ": 0x03, "DAN_DK": 0x04, "DUT_NL": 0x05,
    "FIN_FI": 0x06, "FRE_FR": 0x07, "GER_DE": 0x08, "ITA_IT": 0x0B, "JPN_JP": 0x0C, "KOR_KR": 0x0D,
    "NOR_NO": 0x0E, "POL_PL": 0x0F, "POR_BR": 0x11, "RUS_RU": 0x12, "SPA_ES": 0x13, "SPA_MX": 0x14,
    "SWE_SE": 0x15,
}

DBPF_HEADER_SIZE = 96


class BuildError(Exception):
    """An error that stops the build. The tool shows the text and returns 1."""


_fnv_module = None


def _fnv():
    """Load the FNV-1 hashes of the mod from their file, so that the tool and the mod use one function.

    The tool loads only fnv.py, not the package: the import of the package starts the mod.
    """
    global _fnv_module
    if _fnv_module is None:
        path = SOURCE / PACKAGE_NAME / "fnv.py"
        spec = importlib.util.spec_from_file_location(PACKAGE_NAME + "_build_fnv", str(path))
        module = importlib.util.module_from_spec(spec)
        write_bytecode = sys.dont_write_bytecode
        sys.dont_write_bytecode = True
        try:
            spec.loader.exec_module(module)
        finally:
            sys.dont_write_bytecode = write_bytecode
        _fnv_module = module
    return _fnv_module


# ---------------------------------------------------------------------------------------------------------------------
# DBPF 2.1


def write_dbpf(resources):
    """Return the bytes of a DBPF 2.1 package. resources is a list of ((type, group, instance), bytes)."""
    keys = [key for key, _ in resources]
    if len(set(keys)) != len(keys):
        raise BuildError("two resources have the same key")
    body = io.BytesIO()
    entries = []
    offset = DBPF_HEADER_SIZE
    for (resource_type, group, instance), data in resources:
        entries.append((resource_type, group, instance, offset, len(data)))
        body.write(data)
        offset += len(data)
    index = io.BytesIO()
    # The flags of the index: 0, so each entry has its type, group and instance.
    index.write(struct.pack("<I", 0))
    for resource_type, group, instance, data_offset, size in entries:
        index.write(struct.pack("<IIII", resource_type, group, instance >> 32, instance & 0xFFFFFFFF))
        # Bit 31 of the file size tells that the entry has the compression field.
        index.write(struct.pack("<III", data_offset, size | 0x80000000, size))
        # Compression 0 (none), and 1 for a committed entry.
        index.write(struct.pack("<HH", 0, 1))
    index_bytes = index.getvalue()
    header = bytearray(DBPF_HEADER_SIZE)
    header[0:4] = b"DBPF"
    struct.pack_into("<II", header, 4, 2, 1)  # The file version 2.1.
    struct.pack_into("<I", header, 36, len(entries))
    struct.pack_into("<I", header, 44, len(index_bytes))
    struct.pack_into("<I", header, 60, 3)  # The index version 3.
    struct.pack_into("<Q", header, 64, offset)
    return bytes(header) + body.getvalue() + index_bytes


def read_dbpf(data):
    """Return the resources of a DBPF 2.x package: a dict (type, group, instance) to bytes.

    The reader accepts only uncompressed resources, because the build makes only these.
    """
    if data[:4] != b"DBPF":
        raise BuildError("the file is not a DBPF package")
    major, minor = struct.unpack_from("<II", data, 4)
    if major != 2:
        raise BuildError("the package has the version {}.{}, not 2.x".format(major, minor))
    count = struct.unpack_from("<I", data, 36)[0]
    index_size = struct.unpack_from("<I", data, 44)[0]
    index_offset = struct.unpack_from("<Q", data, 64)[0] or struct.unpack_from("<I", data, 40)[0]
    index = data[index_offset:index_offset + index_size]
    flags = struct.unpack_from("<I", index, 0)[0]
    position = 4
    constant = {}
    for bit, name in ((0, "type"), (1, "group"), (2, "high")):
        if flags & (1 << bit):
            constant[name] = struct.unpack_from("<I", index, position)[0]
            position += 4
    resources = {}
    for _ in range(count):
        values = {}
        for name in ("type", "group", "high"):
            if name in constant:
                values[name] = constant[name]
            else:
                values[name] = struct.unpack_from("<I", index, position)[0]
                position += 4
        low, data_offset, file_size, memory_size = struct.unpack_from("<IIII", index, position)
        position += 16
        compression = 0
        if file_size & 0x80000000:
            compression = struct.unpack_from("<H", index, position)[0]
            position += 4
        file_size &= 0x7FFFFFFF
        if compression != 0 or file_size != memory_size:
            raise BuildError("the reader accepts only uncompressed resources")
        key = (values["type"], values["group"], (values["high"] << 32) | low)
        resources[key] = data[data_offset:data_offset + file_size]
    return resources


# ---------------------------------------------------------------------------------------------------------------------
# String tables


def write_string_table(strings):
    """Return the bytes of a string table (STBL version 5). strings is a list of (key, text)."""
    encoded = [(key, text.encode("utf-8")) for key, text in strings]
    out = io.BytesIO()
    out.write(b"STBL")
    out.write(struct.pack("<HB", 5, 0))
    out.write(struct.pack("<Q", len(encoded)))
    out.write(b"\x00\x00")
    out.write(struct.pack("<I", sum(len(data) + 1 for _, data in encoded)))
    for key, data in encoded:
        if len(data) > 0xFFFF:
            raise BuildError("a string is too long")
        out.write(struct.pack("<IBH", key, 0, len(data)))
        out.write(data)
    return out.getvalue()


def read_string_table(data):
    """Return the strings of a string table: a dict key to text."""
    if data[:4] != b"STBL":
        raise BuildError("the resource is not a string table")
    version, compressed = struct.unpack_from("<HB", data, 4)
    if version != 5 or compressed != 0:
        raise BuildError("the string table has the version {}".format(version))
    count = struct.unpack_from("<Q", data, 7)[0]
    position = 21
    strings = {}
    for _ in range(count):
        key, _, length = struct.unpack_from("<IBH", data, position)
        position += 7
        strings[key] = data[position:position + length].decode("utf-8")
        position += length
    return strings


def string_table_instance(language_code):
    """Return the instance of the string table of one language."""
    return (language_code << 56) | (_fnv().fnv1_64(STRING_TABLE_NAME) & 0x00FFFFFFFFFFFFFF)


def read_strings(path):
    """Return the strings of strings.json as a list of (key, text). The key is the FNV-1 32 of the name."""
    fnv = _fnv()
    with open(str(path), "r", encoding="utf-8") as file:
        names = json.load(file)
    if not isinstance(names, dict):
        raise BuildError("{} must hold a JSON object".format(path))
    keys = {}
    for name, text in names.items():
        key = fnv.fnv1_32(name)
        if key in keys:
            raise BuildError("the strings {} and {} have the same key".format(keys[key], name))
        keys[key] = name
        if not isinstance(text, str) or not text:
            raise BuildError("the string {} is empty".format(name))
    return [(fnv.fnv1_32(name), text) for name, text in sorted(names.items())]


# ---------------------------------------------------------------------------------------------------------------------
# Tuning


def read_tuning(path, string_keys):
    """Check one tuning XML. Return ((type, group, instance), bytes)."""
    fnv = _fnv()
    data = pathlib.Path(path).read_bytes()
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise BuildError("{}: {}".format(path, error))
    if root.tag != "I":
        raise BuildError("{}: the root must be an instance tuning <I>".format(path))
    kind = root.get("i")
    if kind not in TUNING_TYPES:
        raise BuildError("{}: the tuning kind {!r} is not known".format(path, kind))
    name = root.get("n") or ""
    instance = int(root.get("s") or "0")
    if instance != fnv.fnv1_64(name):
        raise BuildError("{}: s must be the FNV-1 64 of the name {!r}: {}".format(path, name, fnv.fnv1_64(name)))
    if root.get("m") != PACKAGE_NAME + ".interaction" and kind == "interaction":
        raise BuildError("{}: the module must be {}.interaction".format(path, PACKAGE_NAME))
    for element in root.iter("T"):
        if element.get("n") == "display_name":
            key = int((element.text or "").strip(), 16)
            if key not in string_keys:
                raise BuildError("{}: display_name {:#010x} is not in strings.json".format(path, key))
    return (TUNING_TYPES[kind], 0, instance), data


def package_resources(tuning_folder=TUNING):
    """Return the resources of the package: the tuning XMLs and the string tables."""
    strings = read_strings(pathlib.Path(tuning_folder) / "strings.json")
    string_keys = {key for key, _ in strings}
    resources = []
    for path in sorted(pathlib.Path(tuning_folder).glob("*.xml")):
        resources.append(read_tuning(path, string_keys))
    if not resources:
        raise BuildError("{} has no tuning XML".format(tuning_folder))
    table = write_string_table(strings)
    for language in sorted(LANGUAGE_CODES):
        resources.append(((TYPE_STRING_TABLE, STRING_TABLE_GROUP, string_table_instance(LANGUAGE_CODES[language])),
                          table))
    return resources


# ---------------------------------------------------------------------------------------------------------------------
# The script


_COMPILE = (
    "import py_compile, sys\n"
    "for source, target, name in zip(sys.argv[1::3], sys.argv[2::3], sys.argv[3::3]):\n"
    "    py_compile.compile(source, cfile=target, dfile=name, doraise=True)\n"
)


def find_python37():
    """Return the python.exe of Python 3.7, or None.

    The environment variable TS4_PYTHON37 gives the folder of Python 3.7. Else the tool uses the Python that runs it if
    that Python is 3.7. Else it asks the Python launcher (py -3.7)."""
    folder = os.environ.get("TS4_PYTHON37")
    if folder:
        return pathlib.Path(folder) / "python.exe"
    if sys.version_info[:2] == (3, 7):
        return pathlib.Path(sys.executable)
    try:
        output = subprocess.run(["py", "-3.7", "-c", "import sys; print(sys.executable)"], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, universal_newlines=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return pathlib.Path(output.strip())


def check_python(python):
    """Refuse a Python that is not 3.7."""
    if python is None:
        raise BuildError("the build needs Python 3.7: set TS4_PYTHON37 to its folder, or give --python")
    python = pathlib.Path(python)
    if not python.exists():
        raise BuildError("{} does not exist: the build needs Python 3.7".format(python))
    output = subprocess.run([str(python), "-c", "import sys; print(sys.version_info[:2])"], check=True,
                            stdout=subprocess.PIPE, universal_newlines=True).stdout.strip()
    if output != "(3, 7)":
        raise BuildError("{} is Python {}, but the game needs Python 3.7".format(python, output))


def source_files(source=SOURCE):
    """Return the source files of the package, in name order."""
    folder = pathlib.Path(source) / PACKAGE_NAME
    files = sorted(path for path in folder.rglob("*.py") if "__pycache__" not in path.parts)
    if not files:
        raise BuildError("{} has no source file".format(folder))
    return files


def build_script(out_file, python=None, source=SOURCE):
    """Compile the sources with Python 3.7 and write the .ts4script zip. Return the names in the zip."""
    if python is None:
        python = find_python37()
    check_python(python)
    files = source_files(source)
    with tempfile.TemporaryDirectory(prefix="ts4_youtube_tv_build_") as temp:
        arguments = []
        names = []
        for path in files:
            relative = path.relative_to(source).as_posix()
            name = relative[:-3] + ".pyc"
            target = pathlib.Path(temp) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            arguments += [str(path), str(target), relative]
            names.append(name)
        subprocess.run([str(python), "-I", "-c", _COMPILE] + arguments, check=True)
        out_file.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(str(out_file), "w", zipfile.ZIP_STORED) as archive:
            for name in names:
                data = (pathlib.Path(temp) / name).read_bytes()
                if data[:4] != PYC_MAGIC_37:
                    raise BuildError("{} is not Python 3.7 bytecode".format(name))
                info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                archive.writestr(info, data)
    return names


def build(out=DEFAULT_OUT, python=None):
    """Build the two files of the mod. Return their paths."""
    out = pathlib.Path(out)
    script = out / SCRIPT_FILE
    package = out / PACKAGE_FILE
    build_script(script, python)
    package.parent.mkdir(parents=True, exist_ok=True)
    package.write_bytes(write_dbpf(package_resources()))
    return script, package


def describe(script, package):
    """Return the lines that describe the built files."""
    lines = ["{} ({} bytes):".format(script, script.stat().st_size)]
    with zipfile.ZipFile(str(script)) as archive:
        for info in archive.infolist():
            lines.append("  {} ({} bytes)".format(info.filename, info.file_size))
    lines.append("{} ({} bytes):".format(package, package.stat().st_size))
    for (resource_type, group, instance), data in sorted(read_dbpf(package.read_bytes()).items()):
        lines.append("  {:08X}:{:08X}:{:016X} ({} bytes)".format(resource_type, group, instance, len(data)))
    return lines


# ---------------------------------------------------------------------------------------------------------------------
# The self-test


def self_test(python=None):
    """Build into a temporary folder and read the files back."""
    fnv = _fnv()
    assert fnv.fnv1_64("TS4_TV_Comedy_Standup") == 0x8A50833624769271
    sample = [((1, 2, 0x1122334455667788), b"abc"), ((TYPE_INTERACTION, 0, 5), b"")]
    assert read_dbpf(write_dbpf(sample)) == dict(sample)
    table = write_string_table([(1, "One"), (0xFFFFFFFF, "\u00e9t\u00e9")])
    assert read_string_table(table) == {1: "One", 0xFFFFFFFF: "\u00e9t\u00e9"}
    with tempfile.TemporaryDirectory(prefix="ts4_youtube_tv_selftest_") as temp:
        script, package = build(pathlib.Path(temp), python)
        with zipfile.ZipFile(str(script)) as archive:
            names = archive.namelist()
            assert PACKAGE_NAME + "/__init__.pyc" in names, names
            assert all(name.startswith(PACKAGE_NAME + "/") and name.endswith(".pyc") for name in names), names
            assert all(archive.read(name)[:4] == PYC_MAGIC_37 for name in names)
        resources = read_dbpf(package.read_bytes())
        interaction_id = fnv.fnv1_64(PACKAGE_NAME + ":WatchYoutubeVideo")
        assert (TYPE_INTERACTION, 0, interaction_id) in resources
        tables = [key for key in resources if key[0] == TYPE_STRING_TABLE]
        assert len(tables) == len(LANGUAGE_CODES)
        strings = read_string_table(resources[tables[0]])
        assert strings[fnv.fnv1_32(PACKAGE_NAME + ":WatchYoutubeVideo.display_name")] == "Watch a YouTube video"
    print("self-test: OK")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build the script mod of the YouTube TV.")
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="the output folder")
    parser.add_argument("--python", help="Python 3.7 to compile the sources (default: TS4_PYTHON37 or py -3.7)")
    parser.add_argument("--self-test", action="store_true", help="build into a temporary folder and check it")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            return self_test(args.python)
        script, package = build(pathlib.Path(args.out), args.python)
        print("\n".join(describe(script, package)))
        return 0
    except (BuildError, subprocess.CalledProcessError) as error:
        print("error: {}".format(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
