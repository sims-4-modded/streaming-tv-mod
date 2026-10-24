# The Sims 4: DLL mod template

The template of a DLL mod (`mod_template.dll`) for the extender of The Sims 4 (the project `mod_loader`). Copy it to
start a new mod. It has the folder structure, the config files, the build and the tests of every mod of the projects.

- `src/core`: the logic of the mod that does not need the game (here: the settings). A static library with tests.
- `src/game`: the DLL entry (`ModMain.cpp`) and the parts that touch the game. Windows only.
- `tests`: the GoogleTest tests of `src/core`, with fakes, never with the game or game data.
- `external/common`: the shared code (git submodule): `Result`, the game version, the C interface of the extender
  (`Ts4ModApi.h`) and its C++ side (`ModHost`).
- `tools/preset.bat`: configure, compile and test in the MSVC x64 environment.

## Make a new mod from the template

On GitHub, use the template repository ("Use this template"). On this computer:

```bat
git clone D:\sims_4\projects\mod_template D:\sims_4\projects\<mod_id>
cd D:\sims_4\projects\<mod_id>
git -c protocol.file.allow=always submodule update --init
python tools\rename_mod.py <mod_id> "<Mod name>"
```

`rename_mod.py` changes the names of the template (the DLL, the targets, the namespace, the classes, the files and the
settings file) to the names of the new mod. Then make a new history (`git checkout --orphan main`), or keep the
history of the template.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout `D:\sims_4\vcpkg`.

```bat
git -c protocol.file.allow=always submodule update --init
tools\preset.bat x64-release
```

The DLL uses the static C runtime (triplet `x64-windows-static`) and brings no other DLL.

## Tests

`ctest --preset x64-release` runs the GoogleTest tests of `src/core`.

## Settings

The mod reads `mod_template.json` in the settings folder that the extender gives. A missing file or field keeps its
default:

```json
{ "enabled": true, "strength": 1.0 }
```

## Install (not done by the build)

The DLL goes into the folder of DLL mods of the extender. Only the install tool of `mod_loader` adds and removes it:

```bat
python ..\mod_loader\tools\install.py mods add build\x64-release\mod_template.dll
```
