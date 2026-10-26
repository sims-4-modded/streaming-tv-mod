# The helper `youtube_tv_host.exe`

`<workspace>` is the folder that holds `projects`.

The helper plays a YouTube video with the official YouTube player (the IFrame Player API) in CEF, without a window.
It gives the frames as I420 planes and the sound as PCM in shared memory. The parent is the DLL mod in the game (and
the probe `host_probe.exe` for tests). A crash of the helper does not stop the game.

The ads and the branding of the player stay. The helper does not download the stream.

## Start and stop

The parent starts the helper with pipes for the standard input and the standard output:

```text
youtube_tv_host.exe --parent-pid=<pid of the parent> [--cache-dir=<folder>]
```

| Option | What |
|---|---|
| `--parent-pid` | The process that gets the handles of the shared memory. Without it, the handles in the events are handles of the helper. |
| `--cache-dir` | The root cache folder of CEF. The default is the folder `cache` next to the exe. CEF also writes `cef.log` there. |

The parent must:

- give the helper only its own ends of the pipes (no other inherited handle);
- put the helper into a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, so that no process of the helper stays
  if the parent stops;
- read the events all the time, on a different thread from the game. The helper writes the events on the UI thread
  of CEF. If the pipe is full, the player stops until the parent reads.

The helper stops when it gets `quit`, when its standard input closes, or when the parent process stops. It closes
each browser, sends `closed` for each video and `bye`, and stops. If CEF does not stop in 10 seconds, the helper stops
its process.

## Commands (parent to helper)

Each command is one JSON object on one line. `id` is a number of 1 or more that the parent selects for each video.

| Command | Fields | What |
|---|---|---|
| `open` | `id`, `videoId` or `url`, `width`, `height`, optional `start`, `fps` (1 to 60, default 30), `matrix` (`bt601` or `bt709`, default `bt601`), `range` (`limited` or `full`, default `limited`) | Make a browser for one video. The helper rounds the width and the height up to multiples of 16 (16 to 4096). |
| `play` | `id` | `playVideo()`. |
| `pause` | `id` | `pauseVideo()`. |
| `setRate` | `id`, `rate` (0.25 to 2) | `setPlaybackRate(rate)`. The player uses the nearest rate that it has. |
| `seek` | `id`, `seconds` | `seekTo(seconds, true)`. |
| `close` | `id` | Close the browser of the video. |
| `quit` | | Close all browsers and stop the helper. |

`url` accepts `youtube.com/watch?v=`, `youtu.be/`, `youtube.com/shorts/`, `youtube.com/embed/`,
`youtube.com/live/` and `youtube-nocookie.com/embed/`, with other parameters; `t` or `start` gives the start time.
The helper refuses all other URLs (`src/core/YoutubeUrl.h`).

Example:

```json
{"cmd":"open","id":1,"url":"https://youtu.be/M7lc1UVf-VE","width":1280,"height":720}
{"cmd":"setRate","id":1,"rate":2}
```

## Events (helper to parent)

Each event is one JSON object on one line, with `event`, and `id` for an event of one video.

| Event | Fields | When |
|---|---|---|
| `hello` | `version` (of the protocol), `cef`, `chromium`, `pid` | The helper started. |
| `opened` | `videoId`, `width`, `height`, `format` (`i420`), `matrix`, `range`, `sharedMemory`, `sharedBytes`, `handleInParent` | The helper made the shared memory. `sharedMemory` is the handle value in the parent process. The parent owns this handle and closes it. |
| `frame` | `frames`, `paintWidth`, `paintHeight` | The first frame is in the shared memory. |
| `ready` | `duration`, `rates` (the rates of the player), `codecs` (the formats that the browser can play) | The player is ready. |
| `state` | `state`: `unstarted`, `ended`, `playing`, `paused`, `buffering`, `cued` | The state of the player changed. |
| `rate` | `rate` | The actual playback rate changed. |
| `quality` | `quality` (for example `hd720`) | The quality of the stream changed. |
| `time` | `time`, `duration`, `rate`, `state`, `frames`, `convertMs`, `audioFrames`, `droppedAudioFrames` | Two times each second. |
| `audio` | `active`, `sampleRate`, `channels`, `sourceChannels`, or `droppedFrames` | An audio stream started or stopped. |
| `error` | `source`, `message`, optional `code` | See below. |
| `closed` | | The browser of the video closed. The parent can close its handle. |
| `bye` | | The helper stops now. |

The `source` of an error:

| Source | What |
|---|---|
| `player` | The YouTube player: `code` 2 (a bad parameter), 5 (the HTML5 player cannot play it), 100 (removed or private), 101 or 150 (the owner does not permit embedded players), 153 (no origin). |
| `page` | The page could not load the IFrame Player API (`code` -1). |
| `load` | The page did not load (`code` is the network error of Chromium). |
| `renderer` | The renderer process of the video stopped. |
| `audio` | The audio capture failed. |
| `command`, `open`, `host` | A command was not correct, or the helper could not do it. |

## The shared memory

The helper makes one block of shared memory (an unnamed file mapping) for each video and copies its handle into the
parent with `DuplicateHandle`. The layout is in `src/core/HostProtocol.h`, with tests in `tests/test_HostProtocol.cpp`.
The parent uses `SharedBlock::Attach` on the mapped view.

| Part | What |
|---|---|
| `SharedBlockHeader` (64 bytes) | `magic` (`YTVH`), `version` (2), `totalBytes`, `audioOffset`, `framesOffset`. |
| `AudioRingHeader` and the samples | The audio ring (see below). |
| `FrameBufferHeader` (192 bytes) | `width`, `height`, `pitchY`, `pitchUV`, the offsets of Y, U and V in a slot, `slotBytes`, `matrix`, `range`, `frameCounter`, `exchange`, `producerIndex`, `consumerIndex`. |
| Three slots | Each slot has a `FrameSlotHeader` (`frameNumber`, `timeUs`) and the planes Y (`width` x `height`), U and V (`width/2` x `height/2`). |

### The frames: a triple buffer

CEF gives each frame as BGRA in `OnPaint`. The helper changes it to I420 directly into its slot
(`ConvertBgraToI420`, `src/core/Yuv.h`): the mean of each 2x2 block gives U and V. The matrix and the range come from
`open`; the default is BT.601 with the limited range (Y 16 to 235, U and V 16 to 240). The values that the game uses
are not yet confirmed.

The three slots work without a lock:

1. The helper writes the slot `producerIndex`. Then it puts that index with the bit 4 (`k_NewFrameBit`) into
   `exchange` with one atomic exchange, and takes the old index as its next slot.
2. The parent calls `TakeNewest()`. If `exchange` has the bit 4, the parent puts its own index into `exchange` with
   one atomic exchange, and takes the old index as its slot. Otherwise it keeps its slot.
3. The parent reads `FrontPlanes()` for as long as it wants. The helper never writes the slot of the parent.

`frameCounter` counts the frames that the helper completed; `FrontFrameNumber()` is the number of the frame in the
slot of the parent. The parent keeps `consumerIndex` in the shared memory, so a new attach (after a reload of the DLL
mod) reads the same slot.

### The audio: a ring

CEF gives the sound of the page in `CefAudioHandler::OnAudioStreamPacket`, as float PCM with one array for each
channel. The helper asks for stereo at 48000 Hz and writes the frames interleaved into the ring
(`src/core/AudioRing.h`, tests in `tests/test_AudioRing.cpp`). The ring keeps 2 seconds (96000 frames).

- Only the helper writes `writeFrame`, `droppedFrames`, `sampleRate` and `streamCounter`. Only the parent writes
  `readFrame` and `missingFrames`. The positions count frames from the start and do not go back.
- If the ring is full, the helper keeps the frames that fit and adds the others to `droppedFrames` (overrun).
- If the parent asks for more frames than the ring has, it gets the frames that are there, and the difference goes to
  `missingFrames` (underrun).
- `DropOldest(keep)` lets the parent remove old frames to keep the delay small.
- `streamCounter` changes when a new audio stream starts (for example after a pause).

Chromium starts with the switch `--disable-audio-output`: its own output goes to a fake device, so the sound does not
play two times. The audio handler still gets the sound. The probe checks this: the helper processes have no audio
session on the output device, and the ring gets the sound in the same time.

## The page

The helper keeps `src/host/player.html` as a resource of the exe. It serves the page from memory at
`https://youtube-tv.invalid/player/<id>.html` with a scheme handler of CEF; the domain `.invalid` never resolves, so
no request for the page goes to a server. YouTube gets this origin in the `Referer` header and in the `origin`
parameter of the player (since 2025 the embedded player needs it, else error 153).

The page loads `https://www.youtube.com/iframe_api` and makes the player with `autoplay`, `controls=0`,
`disablekb`, `playsinline` and `rel=0`. It sends the events as console messages that start with a random nonce of
the browser; the helper ignores all other console messages (also those of the player iframe, which cannot read the
nonce). The helper calls `window.ytv.play`, `pause`, `setRate` and `seek` with `ExecuteJavaScript`.

The main frame cannot go to another page, and the helper blocks popups.

## The threads

| Thread | What it does |
|---|---|
| Main thread (the UI thread of CEF) | `CefRunMessageLoop`, the commands, `OnPaint` and the I420 conversion, the life of the browsers, the events. |
| Command reader | Reads the standard input, reads each command (`ParseHostCommand`), and posts it to the UI thread. |
| Parent watcher | Waits for the parent process to stop, then posts `quit`. |
| Audio thread of CEF | `OnAudioStreamPacket`: writes the ring. |
| IO thread of CEF | Serves the page (`PageSchemeFactory`). |
| Subprocesses | The renderer, the GPU process, the audio service and the network service are the same exe with `--type=...`. |

## Limits and known behaviour

- The playback rate is from 0.25 to 2. The player has the rates 0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75 and 2.
- The player can show ads before or in the video. The IFrame Player API does not tell when an ad plays, and the rate
  can stay at 1 during an ad.
- Videos whose owner does not permit embedded players give the error 101 or 150. Removed or private videos give 100.
- The CEF builds do not play H.264 or AAC. YouTube sends VP9 or AV1 and Opus, which work (`codecs` in `ready`).
- The I420 conversion of one 1280x720 frame takes approximately 3 ms on the UI thread. Paint gives approximately 28
  frames each second at `fps` 30.
- One helper is for one parent: two helpers with the same `--cache-dir` cannot run at the same time.
- The helper runs without the sandbox of Chromium. It only shows the player page and the YouTube player.

## The probe

`build\x64-release\tools\host_probe.exe` does what the DLL mod will do, and checks each step: it opens a video
(`--video`, default `M7lc1UVf-VE`, 1280x720), waits for `playing`, counts the frames, saves one frame changed back
from I420 to BGRA as a PNG file (`--png`, default `<workspace>\screenshots\cef-helper-<date>.png`), reads the audio
ring and computes the RMS, checks that CEF plays no sound itself, tests `pause`, `play` and `setRate 2` (the actual
rate and the speed of the time), and sends `quit`. It writes all events to
`<workspace>\runs\<date>-cef-helper\probe-events.log` (`--log`) and returns 0 if all checks pass. The probe finds
`<workspace>` from `TS4_WORKSPACE`, or as the first folder above its exe that holds `projects`. If it finds no
workspace, give `--png` and `--log`.

## Install

`python tools\install_host.py install` copies the helper and the CEF runtime from `build\x64-release\host` to
`%LOCALAPPDATA%\Sims4Mods\youtube_tv\` (never into the game folder). `uninstall` removes only the files of its
manifest that did not change, and the cache of the helper. `status` shows the version and the state of each file.
`--self-test` tests the tool on a temporary folder. See `docs/cef.md` for the list of files and the licences.
