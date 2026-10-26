# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""The tests of the build tool: the DBPF package, the string tables, the tuning and the .ts4script zip."""

import importlib.util
import io
import os
import pathlib
import sys
import tempfile
import unittest
import zipfile

import support
import build_script_mod as build
from ts4_youtube_tv import fnv
from ts4_youtube_tv import interaction

# The package reader of the research, which reads a package with other code. The environment variable
# TS4_DBPF_REFERENCE_READER gives its file. If it is not set, the test that uses it is skipped.
REFERENCE_READER = pathlib.Path(os.environ["TS4_DBPF_REFERENCE_READER"]) if os.environ.get(
    "TS4_DBPF_REFERENCE_READER") else None


def load_reference_reader():
    spec = importlib.util.spec_from_file_location("dbpf_scan_reference", str(REFERENCE_READER))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PackageTest(unittest.TestCase):
    def test_reads_back_the_written_package(self):
        resources = [((0xE882D22F, 0, 0xFAB3182B15051DA2), b"<I/>"), ((0x220557DA, 0x80000000, 1 << 56), b"x" * 300),
                     ((7, 8, 9), b"")]
        data = build.write_dbpf(resources)
        self.assertEqual(data[:4], b"DBPF")
        self.assertEqual(build.read_dbpf(data), dict(resources))

    def test_refuses_two_resources_with_one_key(self):
        with self.assertRaises(build.BuildError):
            build.write_dbpf([((1, 2, 3), b"a"), ((1, 2, 3), b"b")])

    @unittest.skipUnless(REFERENCE_READER and REFERENCE_READER.exists(),
                         "TS4_DBPF_REFERENCE_READER does not give the reference reader of the research")
    def test_the_reference_reader_reads_the_package(self):
        reader = load_reference_reader()
        resources = build.package_resources()
        data = build.write_dbpf(resources)
        file = io.BytesIO(data)
        entries = reader.read_index(file)
        self.assertEqual(len(entries), len(resources))
        for entry, (key, payload) in zip(entries, resources):
            self.assertEqual(entry[:3], key)
            self.assertEqual(reader.read_resource(file, entry), payload)

    def test_the_string_table_reads_back(self):
        strings = [(fnv.fnv1_32("a"), "Watch a YouTube video"), (5, "\u00c9t\u00e9 \u4e2d")]
        self.assertEqual(build.read_string_table(build.write_string_table(strings)), dict(strings))


class TuningTest(unittest.TestCase):
    def test_the_package_has_the_tuning_of_the_interaction(self):
        resources = dict(build.package_resources())
        key = (0xE882D22F, 0, interaction.INSTANCE_ID)
        self.assertIn(key, resources)
        text = resources[key].decode("utf-8")
        self.assertIn('c="WatchYoutubeVideoInteraction"', text)
        self.assertIn('m="ts4_youtube_tv.interaction"', text)
        self.assertIn('n="{}"'.format(interaction.TUNING_NAME), text)

    def test_the_display_name_is_in_every_string_table(self):
        resources = dict(build.package_resources())
        tables = [data for (resource_type, _, _), data in resources.items() if resource_type == 0x220557DA]
        self.assertEqual(len(tables), len(build.LANGUAGE_CODES))
        key = fnv.fnv1_32("ts4_youtube_tv:WatchYoutubeVideo.display_name")
        for table in tables:
            self.assertEqual(build.read_string_table(table)[key], interaction.DISPLAY_NAME)

    def test_refuses_a_tuning_with_a_wrong_instance(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            (folder / "strings.json").write_text('{"x": "X"}', encoding="utf-8")
            (folder / "bad.xml").write_text(
                '<I c="C" i="interaction" m="ts4_youtube_tv.interaction" n="ts4_youtube_tv:Bad" s="1"/>',
                encoding="utf-8")
            with self.assertRaises(build.BuildError):
                build.package_resources(folder)

    def test_refuses_a_display_name_that_is_not_in_the_strings(self):
        name = "ts4_youtube_tv:Bad"
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            (folder / "strings.json").write_text('{"x": "X"}', encoding="utf-8")
            (folder / "bad.xml").write_text(
                '<I c="C" i="interaction" m="ts4_youtube_tv.interaction" n="{}" s="{}">'
                '<T n="display_name">0x12345678</T></I>'.format(name, fnv.fnv1_64(name)), encoding="utf-8")
            with self.assertRaises(build.BuildError):
                build.package_resources(folder)


class ScriptTest(unittest.TestCase):
    def python37(self):
        if sys.version_info[:2] == (3, 7):
            return pathlib.Path(sys.executable)
        python = build.find_python37()
        if python is not None and python.exists():
            return python
        self.skipTest("Python 3.7 is not installed")

    def test_the_script_is_a_zip_of_python_37_bytecode(self):
        with tempfile.TemporaryDirectory() as temp:
            out = pathlib.Path(temp) / "ts4_youtube_tv.ts4script"
            names = build.build_script(out, self.python37())
            with zipfile.ZipFile(str(out)) as archive:
                self.assertEqual(sorted(archive.namelist()), sorted(names))
                for name in names:
                    self.assertTrue(name.startswith("ts4_youtube_tv/") and name.endswith(".pyc"), name)
                    self.assertEqual(archive.read(name)[:4], build.PYC_MAGIC_37, name)
            self.assertIn("ts4_youtube_tv/__init__.pyc", names)
            self.assertIn("ts4_youtube_tv/interaction.pyc", names)
            sources = {path.stem for path in (pathlib.Path(support.PYTHON) / "ts4_youtube_tv").glob("*.py")}
            self.assertEqual({pathlib.PurePosixPath(name).stem for name in names}, sources)

    def test_refuses_a_python_that_is_not_37(self):
        if sys.version_info[:2] == (3, 7):
            self.skipTest("the test needs a Python that is not 3.7")
        with self.assertRaises(build.BuildError):
            build.check_python(sys.executable)
        with self.assertRaises(build.BuildError):
            build.check_python(os.path.join(support.TEMP, "missing.exe"))


if __name__ == "__main__":
    unittest.main()
