# The Sims 4: YouTube TV

A DLL mod for the Sims 4 mod loader that shows a YouTube video on the screen of a TV in the game.

- A Sim can watch a YouTube video on a TV from the pie menu.
- The helper `youtube_tv_host.exe` plays the video in CEF (Chrome), without a window.
- The DLL mod writes the frames on the screen of the TV, and plays the sound at the position of the TV.
- The script mod gives the TV, the URL and the clock interactions.

## Build

Requirements: Visual Studio with the C++ tools, and the vcpkg checkout of the workspace (`<workspace>\vcpkg`, where
the workspace is the folder that holds `projects`). The helper also needs CEF in `<workspace>\cache\cef`
(`docs/cef.md`).

```bat
git submodule update --init
tools\preset.bat x64-release
python tools\build_script_mod.py
```

A clone of a local repository needs `git submodule update --init`.

## Tests

`ctest --preset x64-release` runs the GoogleTest tests and the script mod tests. These need Python 3.7
(`TS4_PYTHON37`, or `py -3.7`):

```bat
py -3.7 -B -m unittest discover -s tests/python -p "test_*.py"
```

## How to install

The DLL goes into the DLL mods folder of the loader; the script mod goes into the Mods folder of the game.

```bat
python ..\mod_loader\tools\install.py mods add build\x64-release\youtube_tv.dll
python tools\install_script_mod.py install
python tools\install_host.py install
```
