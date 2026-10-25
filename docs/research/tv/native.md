# TV video and audio: the native side

Lane `tv-native`, 2026-10-25. Research only; no game run.

Tree of every RVA in this note: **`TS4_x64_1.128.90.1030`** (the decrypted exe), unless a line names
`Simulation_x64_1.128.90.1030`. The catalogue holds the detail of each function
(`D:\sims_4\ghidra-catalogue\TS4_x64_1.128.90.1030\fn\<RVA>.md`). Names in the form `FUN_<rva>` have no name in Ghidra;
the names in this note are our names (each catalogue entry has the name in its comments).

## Summary

- The native video code is in the exe, not in `Simulation_x64.dll`. `Simulation_x64.dll` only has the protobuf class
  `EA::Sims4::Network::VideoSetPlaylist`. Game Python makes this message and the distributor sends it to the client.
- The decoder is EA RenderWare movie (`rw::movie::MoviePlayer2`) with `rw::movie::VideoDecoder_Vp6` (On2 VP6). The
  resource type of a clip is `0x376840D7` (`VP6Video`). The audio decoders are `AudioDecoder_EAXA` and
  `AudioDecoder_MsImaAdpcm`.
- The decoder writes each frame as three CPU planes (Y, U, V; 4:2:0). `VideoRenderer::RenderFrame` copies the planes
  into three `R8_UNORM` textures and draws them with the effect `VideoDecode` into one `B8G8R8A8_UNORM` render
  target for each channel. The size of the target is the size of the clip.
- `VideoFixupLOD` puts that render target into the screen material of the model (meshes with the material hash
  `0x9841DC4B`), with an aspect fit.
- The best hook point for our frames is `VideoRenderer::RenderFrame` (`0x017C7280`, 2 pointer arguments). Write our
  YUV planes into the frame before the original runs. Then the game uploads and converts them itself, on its own
  thread and context.
- The game has a speed rule: at `mute_speed` and higher, the movie is silent and a replacement sound plays at the
  object; at `distortion_speed` and higher, the screen material gets a distortion flag.
- Audio: EA's own engine (EAAudioCore 10.13, `EA::Audio::Core::*`, output through its `Dac`). There is no XAudio2,
  FMOD or Wwise. The audio listener follows the active camera each frame (`0x01A78940`).

## 1. The chain from Python to the screen texture

| Step | Function | RVA |
|---|---|---|
| Game Python (VideoComponent) makes `VideoSetPlaylist`; the distributor sends it. | protobuf `VideoSetPlaylist` (`Simulation_x64_1.128.90.1030`: vfuncs at `0x00487480`, `0x0048F2A0`, ...) | - |
| The client handles the op: finds the object by id (op +0x28), parses the message, gets the VideoComponent of the object. | `HandleVideoSetPlaylist` | `0x00227300` |
| Sets mute_speed and the replacement sound, and distortion_speed. | `VideoComponent::SetMuteSpeed`, `SetDistortionSpeed` | `0x017C3180`, `0x017C3160` |
| Applies the clips (clear or append, play, loop of the last clip, on and overlay flags). | `VideoComponent::SetPlaylist` | `0x017C3210` |
| Each frame: the service tick gives the clock speed (update context +0x38) to the VideoManager. | `VideoManagerService::vfunc_9` | `0x000AC5E0` |
| Updates each channel; removes the finished ones; presents frames. | `VideoManager::vfunc_8` | `0x017C1E60` |
| One channel: speed rules and the play state machine. | `VideoChannel::Update` | `0x017C4C30` |
| Presents a frame if one is ready. | `VideoChannel::PresentFrame` | `0x017C45C0` |
| Decodes and gives the frame to the renderer (renderer vtable offset 0x80). | `MoviePlayer2::PresentFrame` | `0x017EF6E0` |
| Calls RenderFrame, then gives the old frame back to the decoder. | `VideoRenderer::vfunc_16` | `0x017C7240` |
| Uploads Y, U, V and draws BGRA into the render target. | `VideoRenderer::RenderFrame` | `0x017C7280` |
| Puts the render target into the screen material of the model. | `VideoFixupLOD::vfunc_4` | `0x01B9B110` |

Set-up path: `VideoComponent::OnAdd` (`0x017C2F10`) gets a channel from the VideoManager (`0x017C14B0`). The channel
makes its player in `VideoChannel::OpenPlayer` (`0x017C39B0`) and its render target in
`VideoChannel::EnsureRenderTarget` (`0x017C3E80`). `VideoComponent::AttachFixup` (`0x017C3050`) makes the
`VideoFixupLOD` (`0x01B9B040`) and adds it to the model. When the render target changes, the channel posts the event
`0x59F7C1DA`; `VideoObjectManager::vfunc_1` (`0x017C22E0`) then refreshes the model (`0x017C2EA0`).

### Who owns the frame

| Object | Owner | What |
|---|---|---|
| CPU planes Y, U, V | the frame (renderable) of the MoviePlayer2; `VideoRenderableOperations::vfunc_1` (`0x017C7860`) allocates them | pitch = width rounded up to 16; U and V have half the width and height |
| Plane textures | plane +0x18 | EA textures, `R8_UNORM`, usage 0x0102, made in RenderFrame |
| Render target | channel +0x38, copied to renderer +0x58 | EA texture, `B8G8R8A8_UNORM`, usage 0x0001 (render target + shader resource), clip size |
| D3D11 texture | EA texture +0x28 | `ID3D11Texture2D*` |

Frame layout (from RenderFrame and the renderable operations): planes at frame +0x80 (Y), +0x88 (U), +0x90 (V). A
plane is `{u32 width; u32 height; u8* data; u64 pitch; EaTexture* texture}` at +0x0, +0x4, +0x8, +0x10, +0x18. Frame
+0x58 width, +0x5C height, +0x6C frame index, +0xA1 flip flag.

Channel facts that a mod needs: channel +0x18 channel id, +0xA0 current clip key (u64 instance, u32 type, u32 group).
Renderer +0xA0 is its channel. Component +0x8 is the client object, component +0x20 the channel.

The new black value of a plane is 0x10 for Y and 0x80 for U and V. Thus the frames are limited-range YUV. BT.601 is
probable (VP6); the shader `VideoDecode` has the true matrix.

## 2. Hook points and signatures

All signatures were made with `ghcat.py sig` and are unique in the exe image of this version. The entries keep them.

| Use | Function | RVA | Arguments (Windows x64) | Signature |
|---|---|---|---|---|
| **Main: replace the frames of one screen** | `VideoRenderer::RenderFrame` | `0x017C7280` | RCX renderer, RDX frame. No floats. | `48 8B C4 57 41 54 41 55 41 56 41 57 48 81 EC C0 00 00 00 48 C7 40 80 FE FF FF FF` |
| Same, one level up | `VideoRenderer::vfunc_16` | `0x017C7240` | RCX renderer, RDX frame | `48 89 5C 24 08 57 48 83 EC 20 48 8B FA 48 8B D9 E8 ?? ?? ?? ?? 48 8B 4B 60` |
| Know which component plays which clip | `VideoComponent::SetPlaylist` | `0x017C3210` | RCX component, DX version, R8 clip keys, R9D count (3 more bytes on the stack: final_loop, video_overlay, append_clip) | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 30 48 8B D9` |
| Op handler (object id at op +0x28) | `HandleVideoSetPlaylist` | `0x00227300` | RCX op | `40 55 56 57 48 8D 6C 24 B9 48 81 EC D0 00 00 00 48 C7 45 37 FE FF FF FF` |
| Clock speed of each channel | `VideoChannel::Update` | `0x017C4C30` | RCX channel, EDX clock speed | `48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 48 8B D9` |
| Present path | `VideoChannel::PresentFrame` | `0x017C45C0` | RCX channel | `40 53 48 83 EC 20 48 8B D9 48 8B 49 20 48 85 C9 74 32` |
| Present path | `MoviePlayer2::PresentFrame` | `0x017EF6E0` | RCX player | `40 53 56 57 48 83 EC 40 48 C7 44 24 30 FE FF FF FF 48 8B F9 33 DB` |
| Player set-up | `VideoChannel::OpenPlayer` | `0x017C39B0` | RCX channel | `48 8B C4 57 48 83 EC 60 48 C7 40 D8 FE FF FF FF 48 89 58 08 48 89 70 18 0F 29 70 E8` |
| Camera distance per object | `VideoObjectManager::Update` | `0x017C2AD0` | RCX manager | `40 55 48 83 EC 70 44 0F 29 44 24 30` |
| Screen material | `VideoFixupLOD::vfunc_4` | `0x01B9B110` | RCX fixup, RDX model LOD, R8, R9 | `48 8B C4 55 41 56 48 8D A8 98 FE FF FF` |
| Audio listener from camera | `UpdateListenerFromCamera` | `0x01A78940` | RCX listener state, RDX audio system, R8 sound interface, R9B flag | `44 88 4C 24 20 55 56 57 41 54 41 56` |
| D3D11 device creation | `RenderDevice::CreateDeviceAndSwapChain` | `0x0129C5F0` | RCX render device, DL flag | `40 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 C0 FE FF FF 48 81 EC 40 02 00 00 48 C7 45 00 FE FF FF FF` |
| Texture upload (not for the hook manager: 6 arguments) | `Texture::UploadMip` | `0x012A57D0` | texture, mip, data, pitch, width, height | `40 55 41 54 41 57 48 81 EC 80 00 00 00` |

The listener and fixup signatures are only the prologue. They are unique now, but they can break more easily on a
new version.

### The recommended design

1. Python plays a marker clip on our TV with `VideoSetPlaylist` (a small VP6 resource of our own, with our own
   instance id, at the resolution that we want, for example 640x368; VP6 needs a width that is a multiple of 16).
   The marker clip gives the size of the render target and keeps the channel in the play state.
2. A pre-hook on `RenderFrame` reads the channel (renderer +0xA0) and its clip key (channel +0xA0). If the key is our
   marker, the hook copies our newest decoded frame (YUV 4:2:0, limited range) into the three plane buffers of the
   frame (frame +0x80/+0x88/+0x90). Then the original runs and the game uploads and converts the frame.
3. The hook does no D3D11 work. It runs on the game's own thread, with the game's own context and locks.
4. A YouTube decoder (libvpx, FFmpeg) gives YUV 4:2:0 frames directly, so no BGRA step is necessary. Scale the frame
   to the plane size of the marker clip.

The `ts4-modern-cpp` rule forbids field offsets in code. This design needs the frame and channel offsets above. One way
that keeps to the rule: read each offset from the code bytes at the match of a signature (for example the displacement
of `MOV R13, [R15 + 0x80]` at `0x017C735A` in RenderFrame). The extender owner must decide this.

Fallback design: a post-hook on `RenderFrame` writes BGRA into the render target (`ID3D11Texture2D*` at render target
+0x28) with `UpdateSubresource`. This needs the D3D11 context and the thread rules of item 3 below, so it is not the
first choice.

## 3. The renderer

- `RenderDevice::CreateDeviceAndSwapChain` (`0x0129C5F0`) calls `D3D11CreateDeviceAndSwapChain` (import thunk
  `0x01438A65`, IAT `0x01DE5650`): driver type UNKNOWN on an adapter, flags `0x20` (`BGRA_SUPPORT`), feature level
  11_0, swap chain `B8G8R8A8_UNORM`, 3 buffers, swap effect DISCARD, flag `ALLOW_MODE_SWITCH`. Then it calls
  `CreateDeferredContext`.
- Render device layout: +0xC9 device lost, +0x1D0 `ID3D11Device1`, +0x1D8 the context of this device (a deferred
  `ID3D11DeviceContext1` on the main device), +0x1E0 the immediate `ID3D11DeviceContext1`, +0x1E8 the lock of the
  immediate context, +0x250 parent device, +0x258 the swap chain interface.
- `GetThreadRenderDevice` (`0x0129C3A0`) reads the render device from thread-local storage. `RenderDevice::GetContext`
  (`0x0129C4E0`): mode 0 gives the own context, mode 1 the immediate context with its lock, mode 2 the immediate
  context without the lock.
- So the game records with a deferred context and other threads take a lock before they use the immediate context.
  A DLL gets the same device with `IDXGISwapChain::GetDevice` in the `Present` hook, and the immediate context with
  `ID3D11Device::GetImmediateContext`. Use the immediate context only on the thread that calls `Present`, and only
  after the inspector proves that this thread executes the game's command lists.
- Texture formats: EA format byte to DXGI by the int table at `0x02982B40`. The video render target is EA `0x3D` =
  DXGI 87 `B8G8R8A8_UNORM`; the planes are EA `0x10` = DXGI 61 `R8_UNORM`. Our BGRA frames have the same format as
  the render target.

## 4. Speed

The message fields (from the proto descriptor in the exe): 1 `version_id`, 2 `clip_keys`, 3 `final_loop`,
4 `video_overlay`, 5 `mute_speed`, 6 `distortion_speed`, 7 `speed_audio_clip_replacement` (u64), 8 `append_clip`.

`VideoChannel::Update` (`0x017C4C30`) gets the clock speed of the client each frame (update context +0x38):

- **Mute:** if `mute_speed` is not zero and the speed is `mute_speed` or more, the movie volume is 0. If
  `speed_audio_clip_replacement` is set, `0x017C5310` plays that sound and posts `0xC334D75A`. The VideoObjectManager
  then attaches the sound handle to a component of the object (`0x017C31A0`; probably the AudioComponent), so it
  plays at the TV. Below the speed it stops the sound and posts `0xCFC839EE`.
- **Distortion:** if `distortion_speed` is not zero and the speed is `distortion_speed` or more, the channel sets its
  distortion flag (`0x017C52A0`, event `0x97D12C5D`). `VideoComponent::SetDistortion` (`0x017C3140`) gives it to the
  `VideoFixupLOD` (+0x1A), and the fixup sets the material parameter `0x62644577` to 1. This is a different shader
  state of the screen, not a different clip. The video itself does not play faster.
- **Distance:** `VideoObjectManager::Update` (`0x017C2AD0`) also pauses channels that are far from the camera.

For our mod: Python controls both speed rules. If Python sends `distortion_speed`, the game shows its distortion on
our frames at that speed. Our own audio must follow the same rule (silent at `mute_speed`), and pause at speed 0.

## 5. Audio

- Engine: EA's own EAAudioCore 10.13 (`EA::Audio::Core::*`: `Pan3D`, `SamplePlayer`, `PacketPlayer`, `Dac`,
  `DacNotificationClient` ...). The exe imports `DSOUND.dll` and makes COM objects (`CoCreateInstance`); the
  `IMMNotificationClient` style class suggests WASAPI devices. There is no XAudio2, X3DAudio, FMOD or Wwise.
- Video audio: the movie player can decode EAXA or MS IMA ADPCM audio in the clip. `Video::AudioRenderer` sends the
  packets to the sound manager (`Audio::PacketSubmitCommand`, through `VideoManager::vfunc_1`, `0x017C17C0`). This
  works only when the channel has a sound key (channel +0x78, from component +0x28); the sound handle is then
  attached to the object (event `0xE70746C4`). It is not known if the TV objects set this key. The TV sounds that
  you hear are probably sounds of the interactions (the Python research can confirm).
- Listener: `UpdateListenerFromCamera` (`0x01A78940`, called by the audio system update `0x01A6E8D0`) reads the
  active camera (`GetCameraManager` `0x011CA1B0` -> `GetActiveCamera` `0x0044C850`, camera manager +0x60). It
  compares the 4x4 matrix at camera +0xF0..+0x12F with its copy at listener state +0x40..+0x7F. When it changes, it
  puts the listener at a ratio (listener state +0x84) of the way from the camera position (camera +0x60) to the focus
  point (`0x01209650`: camera +0x240 or +0x200), with a height limit (+0xA0).
- Object position: `VideoObjectManager::Update` gets the world position of each video object from its transform
  component (`0x009B8540`).

What our XAudio2 + X3DAudio player needs from the game:

1. The listener: the camera matrix (camera +0xF0) and position (camera +0x60), or the listener that the game
   computes. A post-hook on `0x01A78940` gives both, once each frame. To sound the same as the game, use the same
   point between the camera and the focus point.
2. The emitter: the TV position and facing. Python can send them once (the TV does not move), or the DLL reads the
   transform component.
3. The clock speed (from `VideoChannel::Update`, or from Python) for mute and pause.
4. The game volume settings (not researched).

## Which TV: the link from a channel to its object

Lane `tv-dll`, 2026-10-25 (catalogue entries `0x00227300`, `0x017C3210`, `0x017C22E0`, `0x017C4C30`, comments by
`tv-dll`).

- The channel has no pointer back to its component or object that we found. The constructor (`0x017C3360`) keeps
  the channel id (+0x18), the player, the renderer, the render target and the clip state, but no owner.
- The only reverse link in the game is the hash map of `VideoObjectManager` (`0x017C22E0`): channel id ->
  VideoComponent (buckets at manager +0x10, count at +0x18, node `{u32 id; +0x8 component; +0x10 next}`), and
  component +0x8 is the client object. A mod would need the instance of the manager and the object id field of the
  client object. Not used.
- **The link that the mod uses:** `HandleVideoSetPlaylist` (`0x00227300`) reads the object id (op +0x28) and calls
  `VideoComponent::SetPlaylist` (`0x017C3210`) with the component of that object. Component +0x20 is the channel, the
  same pointer as renderer +0xA0 in `RenderFrame`. So:
  1. A before-callback on `HandleVideoSetPlaylist` (1 argument) keeps the object id of the message.
  2. A before-callback on `SetPlaylist` (1 argument) reads the channel of the component and links it to that
     object id.
  3. An after-callback on `HandleVideoSetPlaylist` clears the pending id, so that a `SetPlaylist` from another
     path is never linked to a wrong object.
  4. The `RenderFrame` callback reads renderer +0xA0 and finds the session of that channel.
  The script mod must give a playlist to our TV (it must anyway: the channel shows frames only while it plays a
  clip), and each playlist links the channel again. When the game frees a channel and makes a new one at the same
  address, the next playlist of the new owner moves the link.
- **The fallback:** the key of the clip that the channel plays (channel +0xA0, 16 bytes, from `0x017C4E4C`). The
  script mod gives the key that it put on the TV (`YoutubeTvSetScreenKey`, before `YoutubeTvPlay`; by default
  `TS4_TV_Yulelog`, type `0x376840D7`, group 0, instance `0x0D432B7F5C14E35D`). If the channel of a frame is not
  linked to an object with a video, the mod takes the object whose screen key is the clip key of the channel, and
  links the channel to it. Two TVs with the same clip key and no playlist link would both match the first session;
  the first link wins. We cannot make VP6 clips ourselves, so the script mod uses a clip of the game.
- The order in the mod: the channel link, then the clip key, then the session of "any TV" (an inspector test only).

## What is still unknown

- (tv-dll) The thread of `HandleVideoSetPlaylist`, `SetPlaylist`, `RenderFrame` and `UpdateListenerFromCamera`. The
  callbacks of the extender run only on the main thread; on another thread only the original runs.
- (tv-dll) That op +0x28 is the u64 object id that game Python uses (the lookup `0x00162030` takes a pointer to it).

- The thread of `RenderFrame` (the main client thread is probable) and the thread that calls `Present`.
- If VP6 keeps its reference frames in the frame planes. The strings `VideoDecoder_Vp6::TransferData` and the
  separate VP6 frame buffers show a copy, so a write into the planes is probably safe.
- The YUV matrix and range in the shader `VideoDecode`.
- The row order and handedness of the camera matrix at camera +0xF0, and if camera +0x60 is the eye position.
- The names of the material hashes `0x9841DC4B`, `0x98CCFBB6`, `0x62644577` (only `0xE576EF37` = FNV-1 of
  `lightmap` is known).
- If TV objects play the audio track of a clip (channel sound key) or only interaction sounds.
- The size of the clips that the game ships for TVs.
- Renames through the server failed: `/rename_function_by_address` changes the name, but the save fails with
  "Unable to lock due to active transaction" (the save runs inside the transaction of the headless script). The
  proposed names are in the comments of the entries.

## What a game run with the inspector must prove

1. Put a TV on, then confirm that `RenderFrame` runs for its channel, about 30 times a second, and on which thread.
2. Read renderer +0x8C/+0x90 (the frame size) and the clip key at channel +0xA0. Confirm the plane layout at frame
   +0x80..+0x90.
3. Fill the Y plane with one value in a pre-hook and confirm that the screen of that TV only changes colour.
4. Confirm that the render target at renderer +0x58 is the same texture that the TV material shows, and its format.
5. Change the game speed and log the EDX of `VideoChannel::Update`, the distortion flag and the volume.
6. Read the camera at camera manager +0x60 and log camera +0x60 and the matrix at +0xF0 while the camera moves.
   Compare them with the listener state in a post-hook on `0x01A78940`.
7. Confirm that `IDXGISwapChain::GetDevice` gives the device at render device +0x1D0.
