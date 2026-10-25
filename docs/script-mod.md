# The script mod

The script mod adds the interaction "Watch a YouTube video" to the pie menu of each TV. It shows a text dialog for the
URL, and it tells the DLL mod `youtube_tv.dll` which TV must show the video. It also sends the speed of the game to the
DLL mod. The DLL mod and the helper `youtube_tv_host.exe` play the video and the audio (`host.md`).

The script mod has two files:

| File | What |
|---|---|
| `ts4_youtube_tv.ts4script` | A zip of Python 3.7 bytecode, with the folder `ts4_youtube_tv` at the root. |
| `ts4_youtube_tv.package` | A DBPF 2.1 package with the tuning of the interaction and a string table for each language. |

The sources are in `python/ts4_youtube_tv` and `tuning`. The research of the game Python is in
`research/tv/python.md`. The C interface of the DLL mod is in `D:\sims_4\progress\youtube_tv_bridge.md` (version 1).

## The parts

| Module | What it does |
|---|---|
| `__init__.py` | The game imports it. It reads the settings, installs the wraps, registers the injection and the cheat commands. An error in one part writes a log line, and the other parts continue. |
| `native.py` | The bridge to `youtube_tv.dll`. The game Python has no `ctypes`, so it first imports the Python module `ts4_youtube_tv_native` that the DLL adds to `sys.modules` (`design.md`). If that module is not there, it uses `ctypes` (if the game has it): it gets the handle of the DLL that the extender loaded (`GetModuleHandleW`), does not load a second copy, and gives each function its types. It checks the API version 1. If the bridge cannot start, it is off with one log line, and the next call tries again. Only this module knows the interface. |
| `urls.py` | The check of the URL of a YouTube video. It has the same rules as `src/core/YoutubeUrl.cpp`, and its tests use the same examples. |
| `fnv.py` | The FNV-1 64 and FNV-1 32 hashes. The game makes the instance of a clip key from the FNV-1 64 of the clip name in lower case. |
| `tv.py` | The test for a TV (the tag `Func_TV` or `Func_Television`, and a Video component), the broken test (`state_component.is_object_usable()`), the transform, the nearest TV and the key of a clip. |
| `watch.py` | Finds the watch interaction of the game on the TV (a subclass of `WatchSuperInteraction`) and pushes it on the Sim. |
| `channel_audio.py` | Silences the sound of the game channel on a TV that shows a video of the mod. |
| `speed_override.py` | Sets `mute_speed` and `distortion_speed` of the Video component of the TV of a session to `SUPER_SPEED3`, and writes the old values back when the session stops. |
| `interaction.py` | `WatchYoutubeVideoInteraction`, a subclass of `ImmediateSuperInteraction`. Its test passes only for a TV that is not broken, when the bridge is ready and the mod is on. It shows the URL dialog. |
| `dialogs.py` | The URL dialog (`UiDialogTextInputOkCancel` from its factory default) and the error notification (`UiDialogNotification`). All texts are raw text. |
| `injection.py` | Adds the interaction to each object tuning class with a Video component, when the OBJECT instance manager completes its load. |
| `clock.py` | Sends the speed of the game to the DLL mod. |
| `session.py` | One session for each TV that shows a video, as long as the watch interaction of the Sim runs. It starts and stops the sessions. |
| `commands.py` | The cheat commands. |
| `settings.py` | The settings file. |
| `log.py` | The log file. |

Each callback, wrap and cheat command catches each exception, writes it to the log, and calls the original behaviour
of the game.

## The flow of the interaction

1. The player opens the pie menu of a TV. The test of the interaction passes for a TV that works.
2. The player selects "Watch a YouTube video". The dialog shows a text box for 200 characters, with OK and Cancel.
3. Cancel: nothing occurs.
4. OK with a text that is not the URL of a YouTube video: a notification shows the error.
5. OK with a correct URL: the mod pushes the watch interaction of the game on the active Sim (else on the Sim that
   ran the interaction), with the TV as the target (`watch.py`). Then it calls `YoutubeTvPlay` with the object id of
   the TV, the URL, the position and the orientation (a quaternion), and it silences the sound of the channel on that
   TV (`channel_audio.py`).
6. The Sim goes to the TV, the watch interaction puts the TV on its channel, and the Sim sits or stands and watches.
   The channel sets the playlist of the TV. The DLL mod links the channel to the object id of the TV (the playlist
   message), and it shows the frames of the video in place of the frames of the channel.
7. The video stops when the watch interaction ends: the Sim stops watching, the player cancels the interaction, the
   player picks another channel (the game cancels the watch on a change of the channel), or the TV turns off.
8. If the Sim cannot watch the TV, or the DLL mod refuses the video, a notification shows the error. If the DLL mod
   refuses the video, the mod cancels the watch interaction.

The mod gives the URL to the DLL mod as the player typed it, without the spaces at the start and at the end.

## The game functions that the mod wraps or uses

Each wrap keeps the original function and calls it. A second install does not wrap a function two times.

| Game function | What the mod does |
|---|---|
| `clock.GameClock._sync_clock_and_broadcast_gameclock` | Calls the original first. Then it sends `(mode, scale, super_speed)` to `YoutubeTvSetSpeed`. |
| `objects.components.video.VideoComponent.set_video_clips` | Calls the original first. While the watch interaction of the session runs, a playlist does not stop the session (the channel of the watch sets it). If the TV has a session and its watch interaction does not run, the session stops. |
| `objects.components.video.VideoComponent.add_video_clips` | The same as `set_video_clips`. |
| `objects.components.video.VideoComponent.on_remove` | Stops the session of the TV, then calls the original. This occurs when the player sells or removes the TV. |
| `zone.Zone.on_teardown` | Stops all sessions (`YoutubeTvStopAll`), then calls the original. This is the zone unload: `Zone.on_teardown` calls `on_zone_unload` of each service, and the objects of the zone still exist at that time. |
| The OBJECT instance manager: `add_on_load_complete` | Registers the injection. |
| `GameObject.register_on_location_changed` | The mod registers a callback for each TV with a session. When the TV moves, the callback sends the new transform (`YoutubeTvSetTransform`). |
| `Sim.push_super_affordance` | Pushes the watch interaction (`InteractionContext` with `SOURCE_SCRIPT_WITH_USER_INTENT`, `Priority.High`). |
| `Interaction.register_on_finishing_callback` | The end of the watch interaction stops the session. |
| `Interaction.cancel` | `youtube_tv.stop` and a refusal of the DLL mod cancel the watch interaction (`FinishingType.USER_CANCEL`). |
| `VideoComponent.mute_speed`, `VideoComponent.distortion_speed` (values of the component of one TV, not of the class) | At the start of a session, set to `SUPER_SPEED3`. At the stop, the old values come back. |
| `ClientObjectMixin._resend_video_playlist` | Sends the current playlist again after the mod changes its `mute_speed` and `distortion_speed` (at the start, and at the stop except at the zone unload and the removal of the TV). |
| `objects.components.state.StateComponentManagedDistributables.apply` | Calls the original first. Then, if the change is `audio_state` and the target is a TV that shows a video of the mod, the mod stops the new sound. |

The speed values:

- `mode`: the `clock.ClockSpeedMode` value of `GameClock.clock_speed`, as the game has it: PAUSED 0, NORMAL 1,
  SPEED2 2, SPEED3 3, INTERACTION_STARTUP_SPEED 4, SUPER_SPEED3 5. The client gets SUPER_SPEED3 as SPEED3 with a flag,
  but the DLL mod gets 5.
- `scale`: `GameClock.current_clock_speed_scale()`.
- `super_speed`: 1 if the mode is SUPER_SPEED3, else 0.

The game applies its own TV effect at a high speed. The client reads `mute_speed` and `distortion_speed` of the
playlist, and the watch interaction of the game makes the playlist through the channel state. The first game test read
`mute_speed` SPEED2 and `distortion_speed` SPEED2 on `object_tvWallDS2x1_01`: the game distorts the screen and mutes
the channel already at the speed 2.

## The speed override

The decision "Speed 2 and 3": the video plays at the rate 2 with its audio at the speeds 2 and 3, and only the super
speed gets the effect of the game. `speed_override.py` does this:

- `VideoComponent.set_video_clips` makes each new `VideoPlaylist` from the values `self.mute_speed` and
  `self.distortion_speed` of the component (the tuning gives them to `__init__`; each object has its own component).
  `VideoSetPlaylistOp` reads the values of the playlist. Thus a change of the two values on the component of one TV
  goes into each new playlist of that TV only, with no wrap of the op or of the field.
- The current playlist keeps its own copy, and `add_video_clips` adds to it. At the start of the session, the mod also
  changes the two values of the current playlist and calls `_resend_video_playlist` of the TV (the call that the game
  uses after `add_video_clips`). The client then gets the same playlist (same version) with `SUPER_SPEED3`.
- At each stop of the session (the end of the watch, `youtube_tv.stop`, a playlist after the end of the watch, a new
  video, the removal of the TV, the zone unload), the old values go back to the component and to the current playlist.
  The mod sends the playlist again, except at the zone unload and the removal of the TV. A new video on the same TV
  keeps the old values of the first session and sends the playlist one time.
- `youtube_tv.info` shows `SUPER_SPEED3` while a session runs; the log line "speed_override: ... (old values: ...)"
  has the tuned values.

At the super speed, the DLL mod pauses the video, keeps the last frame and stops the audio (`design.md`, the speed
policy), and the client shows its distortion and mutes.

A note on `set_video_clips`: the game also gives the function to the object (`obj.set_video_clips`). That copy calls
the original function of the component, not the wrap. The research found no caller of that copy. The state changes of
the TV, the channels and the cheat command `video.set_clips` use the component, so the wrap sees them.

## The injection

When the OBJECT instance manager completes its load, `injection.py` finds the tuning of the interaction in the
INTERACTION instance manager. It adds the interaction to `_super_affordances` of each object tuning class that has a
tuned Video component (`cls._components_native.Video`; the game tunes it as a native component, the first game test
found 0 classes with `cls._components.Video`). Computers, tablets and consoles also have a Video component; the test
of the interaction hides it on these objects, because they do not have the tag `Func_TV`. The injection adds the
interaction only one time to each class.

## The tuning of the interaction

`tuning/ts4_youtube_tv_WatchYoutubeVideo.xml` (resource type `0xE882D22F` INTERACTION, group 0):

| Field | Value | Why |
|---|---|---|
| `c` | `WatchYoutubeVideoInteraction` | The class of the interaction. |
| `m` | `ts4_youtube_tv.interaction` | The module of the class, in the `.ts4script`. |
| `i` | `interaction` | The kind of instance tuning. |
| `n` | `ts4_youtube_tv:WatchYoutubeVideo` | The unique name of the tuning, with the name of the mod as the prefix. |
| `s` | `18064809103628508578` (`0xFAB3182B15051DA2`) | The instance id: the FNV-1 64 of `n` in lower case. The build tool refuses a different value. |
| `display_name` | `0x260C9DE0` | The key in the string table of the mod: the FNV-1 32 of `ts4_youtube_tv:WatchYoutubeVideo.display_name`. A tuned name must be a string key; the tuning cannot hold raw text. |
| `allow_user_directed` | `True` | The player can select the interaction. |
| `allow_from_world` | `True` | The interaction shows on a TV in the world. |
| `target_type` | `OBJECT` | The target is an object, not a Sim or a group. |

`ImmediateSuperInteraction` locks the other fields that matter: no autonomy, the interaction does not show in the
queue, no constraints. The class also gives its name as raw text (`_get_name`), so the pie menu shows the same name in
each language of the game. The string table is a second source of the name.

The string table (resource type `0x220557DA`, group `0x80000000`, version 5) comes from `tuning/strings.json`. The
instance is the language code in the high byte, and the low 56 bits of the FNV-1 64 of `ts4_youtube_tv:strings`. The
build writes one table for each language code, all with the English text.

## The package

`build/script_mod/ts4_youtube_tv.package` (2031 bytes, uncompressed resources):

| Type | Group | Instance | What |
|---|---|---|---|
| `E882D22F` | `00000000` | `FAB3182B15051DA2` | The tuning XML of the interaction (360 bytes). |
| `220557DA` | `80000000` | `xx4B2997C39CE7C9` | The string table (49 bytes) for each language code `xx`: 00 to 08, 0B to 0F, 11 to 15. |

The package has no resource of EA. The build tool reads the package back, and the tests also read it with the
reader of the research (`D:\sims_4\experiments\tv-mod-research\dbpf_scan.py`).

## The channel and its sound

The watch interaction of each channel (`tv_WatchNews`, `tv_WatchAction`, `tv_WatchMusic`, `tv_WatchCooking`,
`tv_WatchKids`, `tv_WatchFireplace`; names from the tuning) is a `WatchSuperInteraction`
(`objects.electronics.television`). It puts the TV into the state value of its channel, and the last Sim puts the TV
into the off state at the end. A channel is a `VideoChannel` state value with two client state changes:

- `video_state`: the clip list. The state component calls `set_video_clips` of the Video component, and the game sends
  the playlist to the client. The DLL mod gets the object id from that message and links the channel.
- `audio_state`: a sound on the TV (`TunablePlayAudio`, a `PlaySound` distributable). The state component keeps it in
  its managed distributables with the key `("audio_state", state)`.

The video of the mod has its own 3D audio. Thus the mod stops the channel sound of a TV with a session: at the start of
the session, and after each new `audio_state` of that TV (`channel_audio.py`). The sound stays in the map of the game,
so the game stops or replaces it as usual. When the session stops, the mod starts the sounds of the TV again.

The choice: the script mod silences the sound in the simulation, because the channel sound is a sound of the state
value, not the audio track of the clip. If the next game test shows that the clip also plays its own audio (the sound
key of the channel in the client), the DLL mod must force the volume of our channel to 0 in a hook on the client thread
(`VideoChannel::Update`, offsets from code). The research did not find a sound key for the TV objects.

## The settings

The file `%LOCALAPPDATA%\Sims4Mods\youtube_tv_script.json` is optional. A missing field or an incorrect value keeps
the default, with one log line.

| Field | Default | What |
|---|---|---|
| `watch_affordances` | `["tv_WatchNews", "tv_WatchAction", "tv_WatchMusic"]` | The names of the watch interactions, in the order that the mod tries them. If no name matches, the mod takes the first watch interaction of the TV. 1 to 16 names of letters, digits, `_` and `-`. |
| `enabled` | `true` | `false` hides the interaction and stops the cheat command `youtube_tv.play`. |
| `test_url` | `""` | The URL of `youtube_tv.watch_test` (a test in the game; the console makes a URL lower case). |
| `speed_override` | `true` | `false` keeps the `mute_speed` and `distortion_speed` of the TV (a test in the game). |
| `speed_override_resend` | `true` | `false` changes the values without a new send of the current playlist (a test in the game). |

The log of the script mod is `%LOCALAPPDATA%\Sims4Mods\logs\youtube_tv_script.log`. The mod never writes into the game
folder.

## The cheat commands

| Command | What |
|---|---|
| `youtube_tv.play <url>` | The active Sim watches the video on the TV that is nearest to it. If the lot has no TV that works, the command tells you. The cheat console makes the text lower case (first game test), and YouTube video ids have upper case letters, so use the dialog for real videos. |
| `youtube_tv.stop` | Stops every video of the mod and cancels the watch interactions. |
| `youtube_tv.status` | Shows the bridge (ready or off, and why), the settings, the sessions, the last speed that the mod sent, and the status of the DLL mod (`renderCalls`, `framesWritten`, the audio). |
| `youtube_tv.info` | Shows the Video component of the nearest TV: `video_display_type`, `mute_speed`, `distortion_speed`, `speed_audio_clip_replacement` and the playlist with its clip keys. This answers open point 1 of the research. |
| `youtube_tv.watch_test` | Does the same as the OK of the dialog, with `test_url`. For a test in the game without the dialog. |
| `youtube_tv.watch_game` | Pushes only the watch interaction of the game on the nearest TV, with no video of the mod. |
| `youtube_tv.camera [distance] [angle] [height]` | Moves the camera near the nearest TV: the front of the TV turned by the angle (degrees), at the distance and the height (defaults 2.5, 0, 1.5). It writes the old camera and the TV transform to the console. |
| `youtube_tv.camera_reset` | Gives the camera back to the player. |
| `youtube_tv.frames <video\|off\|bars>` | Tells the DLL mod what to write into the frames (`set_frame_mode` of its Python module). |

## Build, test and install

```
python tools/build_script_mod.py               # writes build/script_mod/
python tools/build_script_mod.py --self-test
C:\lib\python37\python.exe -B -m unittest discover -s tests/python -p "test_*.py"
ctest --preset x64-release                     # includes the three script_mod tests
python tools/install_script_mod.py status
python tools/install_script_mod.py install     # copies the two files to Mods\ts4_youtube_tv
python tools/install_script_mod.py uninstall
```

- The build compiles with `C:\lib\python37\python.exe` only, and checks the magic `42 0D 0D 0A` of each `.pyc`.
- The tests run with Python 3.7 and fake game modules (`tests/python/fakes`). They set `LOCALAPPDATA` to a temporary
  folder, so the log and the settings of the tests never go to the folders of the user.
- The install tool copies only the two files into `Documents\Electronic Arts\The Sims 4\Mods\ts4_youtube_tv\` (one
  folder level below `Mods`, as the game permits for a `.ts4script`). It refuses to replace or remove a file that is not
  ours, and it never changes other files. It does not make the `Mods` folder. `status` warns if a copy is also in the
  root of `Mods`.
- The DLL mod must be installed through the extender, and the helper through `tools/install_host.py`. Without the DLL
  mod, the interaction does not show, and the log has one line that tells why.

## The first game test (2026-10-25)

The run notes are in `D:\sims_4\runs\2026-10-25-tv-first\notes.md`. Proved: the script mod loads (after the Python
module of the DLL replaced `ctypes`), the injection (38 classes), the pie menu entry, the dialog and its characters
(`?`, `&`, `=`, `#` and the case come back without a change), the clock values (pause 0/0.0, normal 1/1.0, speed 2
2/3.0, speed 3 3/7.0), `youtube_tv.info` (`mute_speed` SPEED2, `distortion_speed` SPEED2,
`speed_audio_clip_replacement` None on `object_tvWallDS2x1_01`), and the stop of a session on a playlist of the game.
Found: a TV that is off shows no picture, and the cheat console makes the URL lower case. A second URL dialog did not
take input (the owner of the dialog was a different Sim, or the synthetic input); the stacks show no wait in the mod.

## What the next game test must prove

Do the pie menu and the dialog by hand (not with synthetic input).

1. "Watch a YouTube video" on a TV that is off: the active Sim goes to the TV, the TV turns on, the Sim watches, and
   the video shows on the screen (the right way up, correct colours). The log has "watch: pushed tv_WatchNews".
2. The channel sound of the game does not play (log line "channel_audio: stopped the channel sound"), and the 3D audio
   of the video plays from the TV. If a sound of the game still plays, it is the audio of the clip: see "The channel
   and its sound".
3. The session stays while the Sim watches (no "the game called set_video_clips"). It stops when the Sim stops
   watching, when the player cancels the watch, when the player picks another channel, and when the TV turns off (log
   line "the watch interaction ended"). The channel sound plays again after the stop, if the TV stays on.
4. `youtube_tv.stop` cancels the watch interaction.
5. The size of the planes of the channel clip (log line "the first frame on the screen ... into WxH").
6. A second "Watch a YouTube video" on the same TV replaces the video; a second dialog takes input.
7. The speeds with a video: pause, speed 2 and 3, super speed, and the distortion of the game at speed 2.
8. The zone unload with a session, and no exception log.

## Open points

1. Whether the clip of a channel plays its own audio in the client (then the DLL mod mutes our channel).
2. Whether the client of the game limits the text box to fewer than 200 characters.
3. (Decided: "Speed 2 and 3", see "The speed override".) The game distorted and muted the TV at the speed 2.
4. The language codes of the string tables: the game folder has only `Strings_ENG_US.package`, so the other codes come
   from the community list, not from the game. A wrong code has no effect, and the raw text name does not need them.
