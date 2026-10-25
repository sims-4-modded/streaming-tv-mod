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
| `native.py` | The bridge to `youtube_tv.dll` through `ctypes`. It gets the handle of the DLL that the extender loaded (`GetModuleHandleW`), and it does not load a second copy. It gives each function its argument types and result type, and it checks `YoutubeTvApiVersion() == 1`. If the DLL is not loaded or has a different version, the bridge is off, with one log line. Only this module knows the C interface. |
| `urls.py` | The check of the URL of a YouTube video. It has the same rules as `src/core/YoutubeUrl.cpp`, and its tests use the same examples. |
| `fnv.py` | The FNV-1 64 and FNV-1 32 hashes. The game makes the instance of a clip key from the FNV-1 64 of the clip name in lower case. |
| `tv.py` | The test for a TV (the tag `Func_TV` or `Func_Television`, and a Video component), the broken test (`state_component.is_object_usable()`), the transform, the nearest TV and the screen clip key. |
| `interaction.py` | `WatchYoutubeVideoInteraction`, a subclass of `ImmediateSuperInteraction`. Its test passes only for a TV that is not broken, when the bridge is ready and the mod is on. It shows the URL dialog. |
| `dialogs.py` | The URL dialog (`UiDialogTextInputOkCancel` from its factory default) and the error notification (`UiDialogNotification`). All texts are raw text. |
| `injection.py` | Adds the interaction to each object tuning class with a Video component, when the OBJECT instance manager completes its load. |
| `clock.py` | Sends the speed of the game to the DLL mod. |
| `session.py` | One session for each TV that shows a video. It starts and stops the sessions. |
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
5. OK with a correct URL: the mod calls `set_video_clips([screen clip], loop_last=True)` on the Video component of
   the TV. Then it calls `YoutubeTvSetScreenKey` with the key of the screen clip, and `YoutubeTvPlay` with the object
   id of the TV, the URL, the position and the orientation (a quaternion).
6. If the DLL mod refuses the video, the mod clears the playlist of the TV and a notification shows the error of the
   DLL mod (`YoutubeTvLastError`).

The mod gives the URL to the DLL mod as the player typed it, without the spaces at the start and at the end.

## The game functions that the mod wraps or uses

Each wrap keeps the original function and calls it. A second install does not wrap a function two times.

| Game function | What the mod does |
|---|---|
| `clock.GameClock._sync_clock_and_broadcast_gameclock` | Calls the original first. Then it sends `(mode, scale, super_speed)` to `YoutubeTvSetSpeed`. |
| `objects.components.video.VideoComponent.set_video_clips` | Calls the original first. Then, if the call did not come from the mod and the TV has a session, the session stops. This occurs when the TV goes off, the channel changes or the TV breaks. |
| `objects.components.video.VideoComponent.add_video_clips` | The same as `set_video_clips`. |
| `objects.components.video.VideoComponent.on_remove` | Stops the session of the TV, then calls the original. This occurs when the player sells or removes the TV. |
| `zone.Zone.on_teardown` | Stops all sessions (`YoutubeTvStopAll`), then calls the original. This is the zone unload: `Zone.on_teardown` calls `on_zone_unload` of each service, and the objects of the zone still exist at that time. |
| The OBJECT instance manager: `add_on_load_complete` | Registers the injection. |
| `GameObject.register_on_location_changed` | The mod registers a callback for each TV with a session. When the TV moves, the callback sends the new transform (`YoutubeTvSetTransform`). |

The speed values:

- `mode`: the `clock.ClockSpeedMode` value of `GameClock.clock_speed`, as the game has it: PAUSED 0, NORMAL 1,
  SPEED2 2, SPEED3 3, INTERACTION_STARTUP_SPEED 4, SUPER_SPEED3 5. The client gets SUPER_SPEED3 as SPEED3 with a flag,
  but the DLL mod gets 5.
- `scale`: `GameClock.current_clock_speed_scale()`.
- `super_speed`: 1 if the mode is SUPER_SPEED3, else 0.

The game applies its own TV effect at a high speed. The client reads `mute_speed` and `distortion_speed` of the
playlist, and the mod makes the playlist through `set_video_clips` of the Video component of the TV. Thus the TV gets
the effect of its own tuning.

A note on `set_video_clips`: the game also gives the function to the object (`obj.set_video_clips`). That copy calls
the original function of the component, not the wrap. The research found no caller of that copy. The state changes of
the TV, the channels and the cheat command `video.set_clips` use the component, so the wrap sees them.

## The injection

When the OBJECT instance manager completes its load, `injection.py` finds the tuning of the interaction in the
INTERACTION instance manager. It adds the interaction to `_super_affordances` of each object tuning class that has a
tuned Video component (`cls._components.Video`). Computers, tablets and consoles also have a Video component; the test
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

## The screen clip

The mod cannot make VP6 clips. It puts a clip of the game on the TV, and the DLL mod shows the frames of the video in
place of the frames of that clip. The mod gives only the name of the clip to `set_video_clips`. The game makes the key
(type `0x376840D7`, group 0, instance FNV-1 64 of the name), and the mod makes the same key for
`YoutubeTvSetScreenKey`. The package holds no EA bytes.

The default clip is `TS4_TV_Yulelog` (instance `0x0D432B7F5C14E35D`):

- It is in the base game (`Data\Client\ClientFullBuild1.package`, 2223420 bytes). The tool of this lane read the
  index of the package; the mod does not copy the clip.
- TVs show it only on the holiday channel. Thus another TV on the lot almost never shows the same clip, and the
  fallback match of the DLL mod (by the clip key) finds the correct TV.
- `TS4_TV_Comedy_Standup` (`0x8A50833624769271`, the key that the research checked) is a good second choice. A TV on
  the comedy channel also shows it, so the fallback match is less sure.

The setting `screen_clip` changes the clip.

## The settings

The file `%LOCALAPPDATA%\Sims4Mods\youtube_tv_script.json` is optional. A missing field or an incorrect value keeps
the default, with one log line.

| Field | Default | What |
|---|---|---|
| `screen_clip` | `TS4_TV_Yulelog` | The name of the game clip that the mod puts on the TV. Letters, digits, `_` and `-` only. |
| `enabled` | `true` | `false` hides the interaction and stops the cheat command `youtube_tv.play`. |

The log of the script mod is `%LOCALAPPDATA%\Sims4Mods\logs\youtube_tv_script.log`. The mod never writes into the game
folder.

## The cheat commands

| Command | What |
|---|---|
| `youtube_tv.play <url>` | Starts the video on the TV that is nearest to the active Sim. If the lot has no TV that works, the command tells you. |
| `youtube_tv.stop` | Stops every video of the mod and clears the screens. |
| `youtube_tv.status` | Shows the bridge (ready or off, and why), the settings, the screen clip key, the sessions and the last speed that the mod sent. |
| `youtube_tv.info` | Shows the Video component of the nearest TV: `video_display_type`, `mute_speed`, `distortion_speed`, `speed_audio_clip_replacement` and the playlist with its clip keys. This answers open point 1 of the research. |

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

## What the first game test must prove

1. The game loads the script mod: the log has the start line and no failed part, and no new exception log.
2. The injection: the log line gives the count of object classes. Each TV of the base game and of the packs shows
   "Watch a YouTube video". A computer, a tablet and a console do not show it.
3. The dialog made from the factory default shows, with the title, the text, OK and Cancel. Cancel does nothing.
4. The text box accepts 200 characters, and the characters `?`, `&`, `=` and `#` come back through the text input
   without a change. Also test them through `youtube_tv.play`.
5. An incorrect URL shows the notification.
6. The clock values: `youtube_tv.status` shows the mode and the scale for pause, normal, speed 2, speed 3 and super
   speed (mode 5, super_speed 1).
7. `youtube_tv.info` on a TV of the base game gives `mute_speed`, `distortion_speed` and
   `speed_audio_clip_replacement`. Record the values in `research/tv/python.md` (open point 1).
8. After `set_video_clips` with `TS4_TV_Yulelog`, the TV shows the Yule log, and `youtube_tv.info` shows the clip key
   `0x0D432B7F5C14E35D`.
9. A channel change, the TV off and a broken TV stop the session (log line "the game called set_video_clips").
10. The zone unload: a travel to another lot and the main menu stop all sessions (log line "the zone unloads"), with
    no exception log.
11. A move of the TV in build mode sends the new transform.

## Open points

1. The test of the dialog through the factory default (`TunableFactory().default`): the game test must prove that the
   default has each attribute that `build_msg` reads.
2. Whether the client of the game limits the text box to fewer than 200 characters.
3. Whether a TV that is off shows the screen clip, or whether the mod must turn the TV on first.
4. Whether the screen clip plays its own audio track. The research did not find a sound key for the TV objects.
5. The language codes of the string tables: the game folder has only `Strings_ENG_US.package`, so the other codes come
   from the community list, not from the game. A wrong code has no effect, and the raw text name does not need them.
