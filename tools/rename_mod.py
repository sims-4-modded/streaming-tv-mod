#!/usr/bin/env python3
# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""Change the names of the template to the names of a new mod.

Usage: python tools/rename_mod.py <mod_id> "<Mod name>"
       python tools/rename_mod.py --self-test

The mod id is in snake case, for example "better_needs". The tool changes the text of the files of the repository and
the names of the files. It does not change external/common, build folders or .git.
"""

import argparse
import pathlib
import re
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_FOLDERS = {".git", "build", "external", ".idea", "__pycache__"}
TEXT_SUFFIXES = {".cpp", ".h", ".txt", ".json", ".md", ".py", ".bat", ".cmake"}
ID_PATTERN = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


def pascal_case(mod_id):
    """Give the PascalCase name of a snake case id: better_needs gives BetterNeeds."""
    return "".join(part.capitalize() for part in mod_id.split("_"))


def replacements(mod_id, mod_name):
    """Give the pairs of old text and new text, the longest first."""
    pascal = pascal_case(mod_id)
    pairs = [
        ("ts4_mod::template_mod", f"ts4_mod::{mod_id}"),
        ("namespace ts4_mod::template_mod", f"namespace ts4_mod::{mod_id}"),
        ("TS4_TEMPLATE_BUILD_TESTS", f"TS4_{mod_id.upper()}_BUILD_TESTS"),
        ("ts4_mod_template", f"ts4_{mod_id}"),
        ("ts4-mod-template", f"ts4-{mod_id.replace('_', '-')}"),
        ("ts4_template_core", f"ts4_{mod_id}_core"),
        ("ts4_template_tests", f"ts4_{mod_id}_tests"),
        ("TemplateSettings", f"{pascal}Settings"),
        ("TemplateMod", f"{pascal}Mod"),
        ("mod_template", mod_id),
        ("Template mod", mod_name),
        ("template mod", mod_name),
        ("The Sims 4: DLL mod template", f"The Sims 4: {mod_name}"),
    ]
    return sorted(pairs, key=lambda pair: len(pair[0]), reverse=True)


def files_of(root):
    for path in sorted(root.rglob("*")):
        if any(part in SKIP_FOLDERS for part in path.relative_to(root).parts):
            continue
        if path.is_file():
            yield path


def rename(root, mod_id, mod_name):
    """Change the text and the file names below root. Return the count of changed files."""
    pairs = replacements(mod_id, mod_name)
    changed = 0
    for path in list(files_of(root)):
        if path.name == "rename_mod.py":
            continue
        if path.suffix in TEXT_SUFFIXES or path.name == "CMakeLists.txt":
            text = path.read_text(encoding="utf-8")
            new_text = text
            for old, new in pairs:
                new_text = new_text.replace(old, new)
            if new_text != text:
                path.write_text(new_text, encoding="utf-8", newline="\n")
                changed += 1
        new_name = path.name
        for old, new in pairs:
            new_name = new_name.replace(old, new)
        if new_name != path.name:
            path.rename(path.with_name(new_name))
    return changed


def self_test():
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        (root / "src" / "core").mkdir(parents=True)
        (root / "external" / "common").mkdir(parents=True)
        (root / "src" / "core" / "TemplateSettings.h").write_text(
            "namespace ts4_mod::template_mod\n{\nstruct TemplateSettings {};\n}\n", encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            "project(ts4_mod_template)\nadd_library(mod_template SHARED)\n", encoding="utf-8")
        (root / "external" / "common" / "keep.h").write_text("mod_template\n", encoding="utf-8")
        rename(root, "better_needs", "Better needs")
        header = root / "src" / "core" / "BetterNeedsSettings.h"
        assert header.exists(), "the header has no new name"
        assert "namespace ts4_mod::better_needs" in header.read_text(encoding="utf-8")
        assert "struct BetterNeedsSettings" in header.read_text(encoding="utf-8")
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        assert "project(ts4_better_needs)" in cmake and "add_library(better_needs SHARED)" in cmake, cmake
        assert (root / "external" / "common" / "keep.h").read_text(encoding="utf-8") == "mod_template\n"
    assert pascal_case("better_needs") == "BetterNeeds"
    assert ID_PATTERN.match("better_needs") and not ID_PATTERN.match("Better-Needs")
    print("self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mod_id", nargs="?")
    parser.add_argument("mod_name", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.mod_id or not args.mod_name:
        parser.error("give the mod id and the mod name")
    if not ID_PATTERN.match(args.mod_id):
        parser.error("the mod id must be in snake case, for example better_needs")
    if args.mod_id == "mod_template":
        parser.error("give a new mod id")
    count = rename(ROOT, args.mod_id, args.mod_name)
    print(f"changed {count} files to the mod {args.mod_id} ({args.mod_name})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
