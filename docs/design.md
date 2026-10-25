# The design of youtube_tv.dll

The DLL mod shows a YouTube video on the screen of a TV in the game, with the audio at the position of the TV. The
helper `youtube_tv_host.exe` plays the video (`docs/host.md`). The script mod gives the TV, the URL, the clock and the
positions. This file tells the parts, the threads, the data flow and the open points. The research is in
`docs/research/tv/` (`native.md`, `python.md`, `offsets.md`).

Status (2026-10-25): built, unit-tested, tested outside the game with the probe, and in the first game test (branch
`test/first-game-run`).

## The parts

| Part | Where | What it does |
|---|---|---|
| Offsets from code | `src/core/CodeOffsets.h`, `VideoLayout.h` | Reads each field offset from one instruction of the game at a signature match, with a range and an alignment check. |
| Game objects | `src/core/GameObjects.h` | Reads the frame, its planes, the channel of a renderer, the clip key, the object id of a message and the camera matrix through `MemoryAccess`. |
| Frame writer | `src/core/FrameWriter.h` | Copies an I420 frame of the helper into the planes of the game. A different size gets a scaled image (bilinear or nearest) with black bars. It never writes outside the rows of a plane. |
| Sessions | `src/core/Sessions.h` | TV object id -> video of the helper, its state, its channel, its screen key and its transform. It returns the commands for the helper. |
| Speed policy | `src/core/SpeedPolicy.h` | `ClockSpeedMode` -> play or pause, the rate and the mute. |
| Audio space | `src/core/AudioSpace.h` | The transform of a TV and the camera matrix -> the emitter and the listener of X3DAudio. The axes of the game are settings. |
| Bridge checks | `src/core/BridgeArgs.h` | The checks of each argument of the C interface and of the queries. |
| Settings | `src/core/YoutubeTvSettings.h` | The object `youtube_tv` of mods.json. |
| Helper client | `src/game/HostClient.h` | Starts the helper in a job object (the job stops it when the client closes), reads its events on a thread, sends commands, maps the shared memory of each video. |
| Audio engine | `src/game/AudioEngine.h` | XAudio2 2.9 and X3DAudio from the system `xaudio2_9.dll` (`LoadLibraryExW` with the system folder only). One source voice for each video. |
| Process memory | `src/game/ProcessMemory.h` | `MemoryAccess` with a check of each page (`VirtualQuery`) before a read or a write. |
| Runtime | `src/game/TvRuntime.h` | Joins the parts. It has no link to the extender, so the probe drives the same code. |
| Extender glue | `src/game/YoutubeTvMod.h`, `ModMain.cpp` | The settings, the function callbacks, the queries and the status. |
| Bridge | `src/game/Bridge.cpp` | The C interface for the script mod (version 1). |
| Probe | `tools/tv_probe` | The local test outside the game (see below). |

## The hooks (function callbacks of the extender)

All in `TS4_x64.exe`. The signatures are in `src/core/VideoLayout.h`; the RVAs are in `docs/research/tv/native.md`.

| Function | Phase | Arguments | What the mod does |
|---|---|---|---|
| `VideoRenderer::RenderFrame` | before | 2: renderer, frame | Finds the session of the channel (renderer -> channel), takes the newest frame of the helper, and writes it into the three planes of the frame. Then the original uploads and shows them. |
| `HandleVideoSetPlaylist` | before | 1: message | Keeps the object id of the message. |
| `HandleVideoSetPlaylist` | after | 1 | Clears the kept object id. |
| `VideoComponent::SetPlaylist` | before | 1: component | Links the channel of the component to the kept object id. |
| `UpdateListenerFromCamera` | after | 1: listener state | Reads the copy of the camera matrix and moves the listener of the 3D audio. |

The mod reads the offsets one time, in `Ts4ModInit`: the extender loads the DLL mods after the exe is decrypted, and
it puts its hooks on the functions after the inits. A hook changes the first bytes of a function, so a signature
does not match after the hook (the first game test, 2026-10-25, saw this). The mod finds each signature with
`FindUniqueSignature` of `mod_game` (read only) and reads the rules in a window of the function. If the exe is not
decrypted at the init, the first callback tries again. A part with a
failed rule stays off, with one log line: the frames (renderer layout), the link of a TV (channel layout), the
listener.

## Which TV

See "Which TV" in `docs/research/tv/native.md`. The order of the match in `SessionManager::MatchChannel`:

1. The channel that a playlist message linked to the object of a session.
2. Else the object whose screen key (from the script mod) is the clip key of the channel. The mod then links that
   channel.
3. Else the session of "any TV" (object id 0, only from the inspector): it takes the first channel that it sees.

## The threads

The game has two threads that the mod uses (the first game test, 2026-10-25). The client main thread is the thread
of the tick of the extender: the extender runs the function callbacks, the tasks and the queries there. The game
Python runs on a different thread (the simulation thread), and so do the cheat commands and the bridge calls of the
script mod. In the test, the bridge refused `YoutubeTvPlay` with "the call is not on the main thread of the game".

| Thread | What it does |
|---|---|
| Client main thread (the tick of the extender) | The callbacks (`RenderFrame`, the playlist, the listener), the queries, the frame writes, the audio positions (`AudioEngine::Update`). Only this thread reads or writes the memory of the game. |
| Thread of the game Python (simulation) | The bridge calls of the script mod (the C interface and the module `ts4_youtube_tv_native`). They change only the sessions and the settings of the runtime. |
| Reader of the helper | Reads the events of the helper, maps the shared memory of `opened`, and queues the events. `Pump` takes them (in the callbacks of each frame, at most every 4 ms, and in `Status`). |
| Writer of the helper | Writes the commands of the queue (`LineQueue`) to the pipe of the helper. A full pipe stops only this thread. |
| Thread of the audio | Owns XAudio2 in the multithreaded apartment of COM (no change to the COM state of a game thread). Every 5 ms it reads each audio ring into the queue of its voice (three buffers of 10 ms). It removes old audio when the ring has more than 200 ms. |
| Threads of XAudio2 | The audio output of Windows. |

The rules:

- The bridge keeps the thread of its first call as the thread of the game Python (`ThreadGate`), and writes it to the
  log one time. It refuses calls on other threads, with one log line.
- `TvRuntime` has one lock. Each public function takes it. The lock covers only short work on the state of the
  runtime: no wait for the helper (commands go to the queue of the writer), and no copy of a frame (`OnRenderFrame`
  finds the video under the lock, copies the frame without the lock, then takes the lock again to count it).
- The start of the helper (`CreateProcessW`) and of the audio engine occur under the lock, one time, in the first
  `Play`.
- The mod takes the client main thread from its first task (`postTask`), because the extender can call `Ts4ModInit`
  on its start thread. The task also adds the Python module.

**Plan B if `RenderFrame` is not on the main thread.** The extender runs a callback only on the main thread; on
another thread only the original runs. Then the frames never change and `youtube_tv.status` shows `renderCalls` 0
while the TV plays. Options for the merger and the extender lane:

1. The extender gains callbacks for a named render thread (a flag in `Ts4FunctionDesc`, or a new
   `addThreadCallback`). The frame write is safe on that thread: it uses only the shared memory of the helper and
   the planes of the frame, and the sessions give it one copy of the channel map under a lock.
2. The frame write moves to a function that runs on the main thread before the upload: `VideoChannel::PresentFrame`
   (`0x017C45C0`) or `MoviePlayer2::PresentFrame` (`0x017EF6E0`) on the same path. The mod then needs the frame
   from the player (more offsets).
3. The same question applies to `UpdateListenerFromCamera`; the plan for the audio is the same.

## The data flow

1. The script mod calls `YoutubeTvSetScreenKey`, then `YoutubeTvPlay(objectId, url, position, orientation)`, and
   gives the TV a playlist with the screen clip (`set_video_clips`).
2. The runtime starts the helper if it does not run, and sends `open` (640x368 by default, BT.601, limited range).
3. The helper sends `opened`; the reader maps the shared memory. The runtime adds a voice for the audio ring.
4. The helper sends `ready`; the runtime sends the rate and `play` (or `pause`) of the speed policy.
5. The game applies the playlist: the callbacks link the channel of the TV to its object id.
6. Each frame of the channel: `RenderFrame` -> the runtime writes the newest I420 frame into the planes.
7. Each frame of the game: the listener follows the camera; `AudioEngine::Update` computes the 3D audio of each
   voice (position, front, top, distance curve, pan) and its volume.
8. The script mod calls `YoutubeTvSetSpeed` at each change of the clock, `YoutubeTvSetTransform` when the TV moves,
   `YoutubeTvStop` or `YoutubeTvStopAll` (zone teardown).

## The speed policy

| `ClockSpeedMode` | Player | Audio |
|---|---|---|
| `PAUSED` (0) | pause | silent |
| `NORMAL` (1), `INTERACTION_STARTUP_SPEED` (4) | rate 1 | on |
| `SPEED2` (2) | rate `speed2Rate` (2) | on |
| `SPEED3` (3) | rate `speed3Rate` (2, the maximum of the player) | on |
| `SUPER_SPEED3` (5), or any mode with the super speed flag | pause; the last frame stays under the distortion of the game | silent |

The decision for the super speed: **pause, and keep the last frame under the distortion of the game.** Reasons:

- The clock of the game is then many times faster than the rate 2 of the player. A video at the rate 2 does not
  follow the game, and its audio is wrong for that speed.
- The game itself mutes the audio of a TV at a high speed (`mute_speed`) and shows a distortion
  (`distortion_speed`). A still frame under the distortion looks like the TV of the game at a high speed.
- A paused player uses no network and no processor time when the game uses the most.

The mute: the audio is silent when the player pauses, at the speed level `muteLevel` and higher (the default is the
super speed only; the levels are 0 paused, 1 normal, 2, 3, 4 super, 5 never), when a video is not in the state
`playing` (buffering, ended, failed), and after `YoutubeTvStopAll` (the zone unloads). The values of `mute_speed` of
the TV tunings are not known yet (`python.md`, open point 1). When they are known, the script mod can give them, or
`muteLevel` can follow them.

## The Python module `ts4_youtube_tv_native`

The game Python has no `ctypes` package (the first game test, 2026-10-25: `ModuleNotFoundError: No module named
'ctypes'`; `Game\Bin\Python\DLLs` has `_ctypes_x64.pyd`, but the game zips have no `ctypes` package). Thus the DLL
adds its own Python module to the interpreter (`src/game/PythonModule.cpp`):

- The task of the main thread runs at each tick until `python37_x64.dll` is loaded and `Py_IsInitialized()` is true.
  Then, under the GIL, it makes the module with `PyModule_New`, adds the functions with `PyCFunction_NewEx`, and puts
  the module into `sys.modules`. The log gives the number of tries.
- It uses `PythonApi`, `GilGuard` and `PyRef` of common `mod_game`, and gets the other exports (`PyModule_New`,
  `PyCFunction_NewEx`, `PyArg_ParseTuple`, `Py_BuildValue`, ...) itself with `GetProcAddress`.
- The functions mirror the C interface and call the same code (`Bridge.h`):

| Python | C |
|---|---|
| `api_version() -> int` | `YoutubeTvApiVersion` |
| `play(object_id, url, (x, y, z), (x, y, z, w)) -> int` | `YoutubeTvPlay` |
| `stop(object_id) -> int` | `YoutubeTvStop` |
| `stop_all()` | `YoutubeTvStopAll` |
| `set_speed(mode, scale, super_speed)` | `YoutubeTvSetSpeed` |
| `set_transform(object_id, (x, y, z), (x, y, z, w))` | `YoutubeTvSetTransform` |
| `set_screen_key(object_id, instance, type, group)` | `YoutubeTvSetScreenKey` |
| `last_error() -> str` | `YoutubeTvLastError` |
| `status() -> str` | `TvRuntime::Status` as JSON text (the same as the query `youtube_tv.status`); the cheat command `youtube_tv.status` shows it. |

A wrong argument type gives a `TypeError`; an exception of C++ gives a `RuntimeError`. `Ts4ModShutdown` removes the
module from `sys.modules`. When the extender gets its own native Python module (stage 4), this module can move there.

`native.py` of the script mod imports `ts4_youtube_tv_native` first. If it is not there, it uses `ctypes` (if the
game has it) with the C interface below. If neither works, the bridge is off with one log line, and the next call
tries again.

## The C interface for the script mod (version 1)

As `D:\sims_4\progress\youtube_tv_bridge.md` gives it: `extern "C"`, `__cdecl`, UTF-8, no exception goes out, each
pointer and value checked (`src/core/BridgeArgs.h`), the last error set by each call (empty after a call that
succeeds).

| Export | Notes of this implementation |
|---|---|
| `YoutubeTvApiVersion` | 1. |
| `YoutubeTvPlay(objectId, url, position3, orientation4)` | Both pointers null: no transform. One pointer null: an error. A URL of 1 to 2048 bytes with no control character. A new video on the same TV replaces the old one. The limit is `maxSessions`. |
| `YoutubeTvStop(objectId)` | 1 if a video stopped, else 0 with "the TV has no video". |
| `YoutubeTvStopAll` | Stops each video and its voice. |
| `YoutubeTvSetSpeed(mode, scale, superSpeed)` | Mode 0 to 5, scale finite and 0 or more, flag 0 or 1. The script mod sends `SUPER_SPEED3` as 5 with the flag 1. |
| `YoutubeTvSetTransform(objectId, position3, orientation4)` | Both pointers necessary; the quaternion is made a unit quaternion. |
| `YoutubeTvSetScreenKey(objectId, instance, type, group)` | Type not 0. The key is kept also before `YoutubeTvPlay`. |
| `YoutubeTvLastError(buffer, size)` | Copies the error with a terminating zero, cut at a full UTF-8 character, and returns the length of the full error (like `snprintf`). A null buffer or the size 0 gives only the length. Before `Ts4ModInit` (or after `Ts4ModShutdown`) each call returns 0 and the error is "the DLL is not ready". |

## The inspector queries

| Query | Parameters |
|---|---|
| `youtube_tv.play` | `objectId` (a number, a text with a decimal or `0x` number, or `"any"`; default `"any"`), `url`, optional `position` [3] and `orientation` [4]. |
| `youtube_tv.stop` | Optional `objectId`; without it, stop each video. |
| `youtube_tv.status` | None. The videos, the speed, the helper, the hooks (which offsets are known), `renderCalls`, `framesWritten`, the audio. |
| `youtube_tv.speed` | `mode`: `paused`, `normal`, `speed2`, `speed3`, `startup`, `super`, or 0 to 5; optional `superSpeed`. |
| `youtube_tv.transform` | `objectId`, `position` [3], optional `orientation` [4]. |

The queries and the bridge use the same code (`TvRuntime`).

## The settings (`youtube_tv` in mods.json)

`enabled`, `helperPath` (empty: `%LOCALAPPDATA%\Sims4Mods\youtube_tv\youtube_tv_host.exe`), `volume` (0 to 1, 0.8),
`audioRange` (the distance of the half volume, 3), `maxSessions` (1 to 8, 2), `width` and `height` (640 and 368),
`frameRate` (30), `matrix` (`bt601`, `bt709`), `range` (`limited`, `full`), `scaleFilter` (`bilinear`, `nearest`),
`speed2Rate`, `speed3Rate` (0.25 to 2), `muteLevel` (1 to 5), and the axes: `rightHanded` (true), `cameraMatrix`
(`worldRows`, `worldColumns`, `viewRows`, `viewColumns`), `cameraLooksAlongNegativeZ` (true), `modelFront`
([0, 0, 1]), `modelTop` ([0, 1, 0]).

## The plan for the Python bridge (stage 4 of the extender)

When the native Python module of the extender exists, the script mod calls these functions of a module of the mod
(for example `_youtube_tv`), and `ts4_youtube_tv/native.py` is the only module of the script mod that changes:

| Python | C++ | Notes |
|---|---|---|
| `play(object_id: int, url: str, position: (x, y, z) = None, orientation: (x, y, z, w) = None) -> None` | `TvRuntime::Play` | Raises `RuntimeError` with the error text. |
| `stop(object_id: int) -> bool` | `TvRuntime::Stop` | |
| `stop_all() -> None` | `TvRuntime::StopAll` | |
| `set_speed(mode: int, scale: float, super_speed: bool) -> None` | `TvRuntime::SetSpeed` | |
| `set_object_transform(object_id: int, position, orientation) -> None` | `TvRuntime::SetTransform` | |
| `set_screen_key(object_id: int, instance: int, type: int, group: int) -> None` | `TvRuntime::SetScreenKey` | |
| `status() -> dict` | `TvRuntime::Status` | The JSON of `youtube_tv.status`. |

The functions run on the main thread under the GIL, as the C interface does now. The argument checks stay in
`BridgeArgs.h`. The C interface can stay for one version, so that an old script mod still works.

## The probe (a real local test, not in CTest)

`build\x64-release\tools\tv_probe.exe [--helper <exe>] [--video <id or URL>] [--tone-only] [--seconds <n>]`. It needs
the network, the helper (the option, else the installed helper, else `build\x64-release\host`) and an audio device.
It returns 0 if all checks pass, 1 if a check fails, 77 if the helper or the audio device is not there.

1. A test tone through the audio engine: 1 s from the left, 1 s from the right.
2. The runtime with fake objects of the game in its own memory (a renderer, a channel, a frame with three planes, a
   playlist message and a component), through the same callbacks as the game: the link of the TV by its playlist,
   the frames of `M7lc1UVf-VE` in planes of the same size (the planes must be the frame of the helper, byte for
   byte) and of a different size (black bars, the image in the middle), and no write outside the planes.
3. The speed: pause at speed 0, play at speed 3 (rate 2), pause at the super speed with the last frame kept.
4. The audio of the video from the left, then from the right.

## What the first game test must prove

1. The extender accepts the five callbacks (one log line each if not), and the log shows "the offsets are read"
   with `renderer`, `channel` and `listener` true.
2. `RenderFrame`, `HandleVideoSetPlaylist`, `SetPlaylist` and `UpdateListenerFromCamera` run on the main thread
   (`youtube_tv.status`: `renderCalls` grows while a TV plays a game clip; `listenerKnown` true).
3. `youtube_tv.play {"url": "M7lc1UVf-VE"}` (any TV) on a TV that is on shows the video on its screen, the right way
   up (the flip flag), with correct colours (else change `matrix` or `range`).
4. With a real object id: the playlist link (`channelKnown` true after the TV changes channel), and the clip key
   fallback (log the clip key of the channel and compare it with `video.object_info`; the order of type and group).
5. The size of the planes of the clips of the TV (the log line "the first frame on the screen ... into WxH").
6. The speeds: pause, speed 2 and 3 at the rate 2, the super speed with the distortion of the game over the still
   frame, and the audio silent where the policy says.
7. The audio: the camera matrix layout and the handedness (move the camera to each side of the TV), the distance
   curve, and that the game's own sound of the TV does not also play.
8. `YoutubeTvStopAll` at a zone change stops each video and voice; no `youtube_tv_host.exe` stays after the game
   stops.

## Open questions

- The thread of each hooked function (plan B above).
- The axes of the game and the layout of the camera matrix (settings until a game test).
- That op +0x28 of `HandleVideoSetPlaylist` is the u64 object id of game Python.
- The values of `mute_speed` and `distortion_speed` on the TV tunings. The bridge of version 1 has no argument for
  them; a version 2 could add `YoutubeTvSetMuteSpeed(objectId, speed)`.
- The YUV matrix and range of the shader `VideoDecode` (BT.601 and the limited range are the defaults).
- If VP6 keeps reference frames in the planes of the frame (then a write could show in the next frame of the game
  clip; the mod writes each frame, so it is probably not visible).
- Changes that external/common could need: none for the first game test. A render-thread callback (plan B, 1) is a
  change of `Ts4ModApi.h`.
