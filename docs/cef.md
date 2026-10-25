# CEF for the helper

The helper `youtube_tv_host.exe` uses a binary distribution of the Chromium Embedded Framework (CEF). The distribution
is not in the repository and not in the game folder. It is in the cache of the projects.

## The distribution

| Item | Value |
|---|---|
| CEF version | `156.0.14+g4fa4ab5+chromium-156.0.8078.48` (channel stable) |
| Chromium version | `156.0.8078.48` |
| Platform and type | `windows64`, `minimal` |
| File | `cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_windows64_minimal.tar.bz2` |
| Source | `https://cef-builds.spotifycdn.com/` (the official builds of CEF; the list is `index.json`) |
| Size of the archive | 175,051,364 bytes (167 MiB) |
| SHA-1 (published) | `72dc1dd1636acc2391609fa6f410aa09f09168d4` |
| SHA-256 (of the downloaded file) | `99f8a6dffd47417e0caee7845a98c549b41f1d4246cd715dec843ca4b699ee57` |
| Folder | `D:\sims_4\cache\cef\156.0.14+g4fa4ab5+chromium-156.0.8078.48\` (444,643,841 bytes after the extraction) |
| Archive kept in | `D:\sims_4\cache\cef\dl\` |

The site of the builds publishes only a SHA-1 for each file, in `index.json` and in a `.sha1` file next to the
archive. The SHA-1 of the downloaded file was the same as both. The SHA-256 above is our own value of the same file.

The `minimal` distribution has all that the helper needs: `libcef.dll`, `libcef.lib`, the resources, the headers and
the source of `libcef_dll_wrapper`. It has no sample applications and no debug files.

## Get the distribution on a new computer

1. Download the file of the table from `https://cef-builds.spotifycdn.com/` (the `+` characters are `%2B` in the URL).
2. Compare its SHA-1 with the table, or with the `.sha1` file at the same URL.
3. Extract it into `D:\sims_4\cache\cef\<version>\` without the first folder of the archive
   (`tar -xjf <file> -C <folder> --strip-components=1`).

To use a different folder, set the cache variable `TS4_CEF_ROOT` (`cmake --preset x64-release -DTS4_CEF_ROOT=<folder>`).

## The build

`cmake/Cef.cmake` finds the distribution through `TS4_CEF_ROOT`. If the distribution is not there, the configure step
writes one message and the build does not make the helper; the core, the DLL mod and the tests still build.

When the distribution is there, the build:

- compiles `libcef_dll_wrapper` from the distribution with the static C runtime (`/MT`, from
  `CMAKE_MSVC_RUNTIME_LIBRARY`), with C++ exceptions and RTTI like our code (the flags `/GR-` and
  `_HAS_EXCEPTIONS=0` of CEF are removed);
- turns off the sandbox of Chromium (`USE_SANDBOX OFF`). With the sandbox, CEF 156 needs `bootstrap.exe` and a client
  DLL in place of a normal exe;
- puts `youtube_tv_host.exe`, the CEF runtime and the licences in `build\x64-release\host\`, and writes the list of
  these files to `youtube_tv_host_files.txt` for `tools\install_host.py`.

## The files that go to the player

`tools\install_host.py install` copies these files to `%LOCALAPPDATA%\Sims4Mods\youtube_tv\` (235 files, 413.5 MiB):

| File | Why |
|---|---|
| `youtube_tv_host.exe` | The helper. It is also each subprocess of CEF. |
| `libcef.dll`, `chrome_elf.dll`, `icudtl.dat`, `v8_context_snapshot.bin` | Necessary for CEF. |
| `resources.pak`, `chrome_100_percent.pak`, `chrome_200_percent.pak`, `locales\*.pak` | The resources of Chromium. |
| `d3dcompiler_47.dll`, `dxcompiler.dll` | The GPU parts of Chromium. |
| `vk_swiftshader.dll`, `vk_swiftshader_icd.json`, `vulkan-1.dll` | The software renderer of Chromium if no GPU works. |
| `licenses\CEF_LICENSE.txt` | The licence of CEF (BSD). It must go with the binaries. |
| `licenses\CHROMIUM_CREDITS.html` | The licences of Chromium and its third-party parts. It must go with the binaries. |

The build does not copy `bootstrap.exe` and `bootstrapc.exe`, because the helper does not use the sandbox.

## Licences

CEF has a BSD licence (`LICENSE.txt` of the distribution). Chromium and its parts have many licences; `CREDITS.html`
of the distribution has all of them. Both files go with each copy of the helper, in the folder `licenses`. The
Spotify builds of CEF do not have the proprietary codecs (H.264, AAC). YouTube then sends VP9 or AV1 video and Opus
audio, which these builds play (see `docs/host.md`).
