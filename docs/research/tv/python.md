# The TV in the game Python

Game version 1.128.90.1030. Research of 2026-10-25 (lane tv-python), from the decompiled game Python in
`D:\sims_4\cache\gameplay\1.128.90.1030\` (EA's code: never commit or publish it). Native side:
`native.md` in this folder.

## How the game knows a TV

- The catalogue tag `Func_TV` (also `Func_Television`): `obj.has_tag(tag.Tag.Func_TV)`. `Tag` is a dynamic enum: read
  the value at run time.
- The object tunings `object_tv`, `object_tvWall*`, `object_tvSurface*`, with the tuned component `Video`. Computers,
  tablets and consoles also have `Video`, so a test for `Func_TV` is necessary.
- The states `object_TV_On` and `object_TV_Off`. A channel is a state value `VideoChannel`.
- The interactions: `objects/electronics/television.py` `WatchSuperInteraction` (tunings `tv_WatchComedy` and the
  other channels), `tv_PickChannel`, `tv_ChannelSurf`, `tv_turnoff`. A change of the state cancels the watch
  interaction.

## How the screen shows a channel

- `objects/components/video.py`: `VideoComponent` (a native component) with the tunables `video_display_type`,
  `mute_speed`, `distortion_speed` and `speed_audio_clip_replacement` (a PROPX key).
- `set_video_clips(clip_names, loop_last, append_clip)` and `add_video_clips(...)` make a `VideoPlaylist` and set
  `owner.video_playlist`.
- `video_playlist` is a distributor field with the op `VideoSetPlaylistOp`: the protobuf
  `DistributorOps_pb2.VideoSetPlaylist` (version_id, clip_keys, final_loop, video_overlay, mute_speed,
  distortion_speed, speed_audio_clip_replacement, append_clip), operation `VIDEO_SET_PLAYLIST` = 64. The client
  (`TS4_x64.exe`) decodes and draws the video.
- A clip name `X` or `X.vp6` gives the resource key: type `0x376840D7`, group 0, instance FNV-1 64 of the name in lower
  case. Checked: `TS4_TV_Comedy_Standup` gives `0x8A50833624769271`, and the key is in the package. The 90 clips (VP6
  in EA's `SCHl` container) are in `Data\Client\ClientFullBuild1.package`.
- Cheat commands: `video.object_info`, `video.set_clips`, `video.add_clips`.

## The clock

- `clock.ClockSpeedMode`: PAUSED 0, NORMAL 1, SPEED2 2, SPEED3 3, INTERACTION_STARTUP_SPEED 4, SUPER_SPEED3 5.
- Read: `services.game_clock_service().clock_speed` and `current_clock_speed_scale()`. Change: `set_clock_speed`,
  `push_speed` and `pop_speed`.
- There is no list of callbacks for a change of the speed. Each change goes through
  `GameClock._update_speed` and `GameClock._sync_clock_and_broadcast_gameclock`, which sends `SetGameTime` to the
  client. A mod wraps `_sync_clock_and_broadcast_gameclock`. The client gets super speed as speed 3 with a flag.
- The effect of a TV at a high speed is in the client: at `distortion_speed` or more the screen gets a distortion
  shader; at `mute_speed` or more the audio of the video stops, and `speed_audio_clip_replacement` plays. The clip does
  not play faster.

## The dialog

- `ui.ui_dialog_generic.UiDialogTextInputOkCancel`. Make it with `TunableFactory().default(owner, ...)` and give each
  attribute that `build_msg` reads. The text comes back in `dialog.text_input_responses[name]` after the listener of
  `add_listener` runs; `dialog.accepted` tells OK from Cancel.
- The default length limit is 20: give `_TextInputLengthFixed(min_length=None, max_length=200)` for a URL.
- No string table is necessary: `LocalizationHelperTuning.get_raw_text(text)`.

## The interaction

- Add the interaction to each object class with a `Video` component when the OBJECT instance manager completes its
  load (`add_on_load_complete`): `cls._super_affordances += (affordance,)`. Test `Func_TV` in `_test`.
- The class: a subclass of `ImmediateSuperInteraction` that overrides `_run_interaction_gen` (show the dialog) and
  `_test`.
- The safe path needs a `.package` with one XML tuning of type `0xE882D22F` (INTERACTION). A string table
  (`0x220557DA`) is necessary only for localized names.

## Positions and audio

- An object: `obj.position`, `obj.orientation`, `obj.transform`, `get_joint_transform_for_joint(name)`.
- The camera: Python has only the values that the client sends from time to time. The live camera comes from the
  native side.
- Audio: Python can only start and stop packaged sounds (`SOUND_START`, `SOUND_STOP`). The DLL mod plays the audio of
  the video itself.

## Open points

1. The values of `mute_speed`, `distortion_speed` and `speed_audio_clip_replacement` on the TV tunings. The first game
   test (2026-10-25, `youtube_tv.info`) read `object_tvWallDS2x1_01`: `mute_speed` SPEED2, `distortion_speed` SPEED2,
   `speed_audio_clip_replacement` None. Other TV tunings are not read yet.
4. (Answered by the first game test.) A TV that is off shows no picture, also with a playlist. The watch interaction
   (`WatchSuperInteraction`) puts the TV on a channel: a `VideoChannel` state value with a `video_state` (the clips)
   and an `audio_state` (a sound on the object, kept in the managed distributables of the state component).
2. A test of the dialog made with the factory default, and the limit of the text box of the client; whether the
   command parser keeps `?`, `&`, `=` and `#`.
3. Whether the client makes a video surface for a clip key that no package has (then no marker clip is necessary).
