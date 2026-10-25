# The field offsets that the DLL mod reads from the code of the game

Lane `tv-dll`, 2026-10-25. Tree of every RVA in this note: **`TS4_x64_1.128.90.1030`** (the decrypted exe,
`D:\sims_4\dump\TS4_1.128.90.1030\TS4_x64.image.bin`). The catalogue entries
(`D:\sims_4\ghidra-catalogue\TS4_x64_1.128.90.1030\fn\<RVA>.md`) have the full disassembly.

The rule of `ts4-modern-cpp`: a field offset of a game object comes only from an instruction of the game. The mod
finds the function by its signature, searches a window of its code for one instruction pattern, and reads the
displacement of that instruction (`src/core/CodeOffsets.h`, the rules in `src/core/VideoLayout.h`). Each rule must
match exactly one place in its window, and its value must be in a range and aligned. If a rule fails, the parts that
need it stay off, with one log line.

`tests/test_CodeOffsets.cpp` keeps the instruction bytes below (only these bytes, no other code of the game).

## The functions and their windows

The signatures are those of `native.md`. Each one matches exactly one place in the image (checked on the dump with
a script on 2026-10-25). Each window is not longer than its function.

| Function | RVA | Window (bytes after the start) |
|---|---|---|
| `VideoRenderer::RenderFrame` | `0x017C7280` | `0x500` (the function is `0x494` bytes) |
| `HandleVideoSetPlaylist` | `0x00227300` | `0x200` |
| `VideoChannel::Update` | `0x017C4C30` | `0x6E0` (the next function starts at `0x017C5310`) |
| `UpdateListenerFromCamera` | `0x01A78940` | `0x200` |

## The rules

Each pattern was checked: exactly one match in its window of the image of this version.

| Field | Instruction (RVA) | Bytes in the image | Pattern in the rule | Value |
|---|---|---|---|---|
| Frame: the pointer to the Y plane | `MOV R13,[R15+0x80]` (`0x017C735A`) | `4D 8B AF 80 00 00 00` | `4D 8B AF ?? ?? ?? ?? 4C 89 6C 24 50 49 8B 87 ?? ?? ?? ?? 48 89 44 24 58 49 8B 87 ?? ?? ?? ?? 48 89 44 24 60` (displacement at 3) | `0x80` |
| Frame: U plane | `MOV RAX,[R15+0x88]` (`0x017C7366`) | `49 8B 87 88 00 00 00` | same pattern, at 15 | `0x88` |
| Frame: V plane | `MOV RAX,[R15+0x90]` (`0x017C7372`) | `49 8B 87 90 00 00 00` | same pattern, at 27 | `0x90` |
| Frame: flip flag (u8) | `MOVZX EAX,byte [R15+0xA1]` (`0x017C756B`) | `41 0F B6 87 A1 00 00 00 84 C0 74 26` | `41 0F B6 87 ?? ?? ?? ?? 84 C0 74` (at 4) | `0xA1` |
| Plane: height (u32) | `MOV EAX,[RBX+0x4]` (`0x017C7477`) | `8B 43 04` | `8B 43 ?? 89 44 24 28 8B 03 89 44 24 20 4C 8B 4B ?? 4C 8B 43 ?? 33 D2 48 8B 4B ??` (at 2) | `0x4` |
| Plane: width (u32) | `MOV EAX,[RBX]` (`0x017C747E`) | `8B 03` | same pattern; the form `[RBX]` gives 0 | `0x0` |
| Plane: pitch (u64) | `MOV R9,[RBX+0x10]` (`0x017C7484`) | `4C 8B 4B 10` | same pattern, at 16 | `0x10` |
| Plane: data (u8*) | `MOV R8,[RBX+0x8]` (`0x017C7488`) | `4C 8B 43 08` | same pattern, at 20 | `0x8` |
| Renderer: height of the target (u32) | `MOV R8D,[RSI+0x90]` (`0x017C731B`) | `44 8B 86 90 00 00 00` | `44 8B 86 ?? ?? ?? ?? 8B 96 ?? ?? ?? ?? 48 8B 4E` (at 3) | `0x90` |
| Renderer: width of the target (u32) | `MOV EDX,[RSI+0x8C]` (`0x017C7322`) | `8B 96 8C 00 00 00` | same pattern, at 9 | `0x8C` |
| Renderer: its channel | `MOV RCX,[RSI+0xA0]` (`0x017C7339`) | `48 8B 8E A0 00 00 00 48 85 C9 74 05 E8` | `48 8B 8E ?? ?? ?? ?? 48 85 C9 74 ?? E8` (at 3) | `0xA0` |
| Message of a playlist: the key of the object | `ADD RCX,0x28` (`0x00227323`), the argument of the lookup of the object | `48 8B D9 48 83 C1 28 48 8D 55` | `48 8B D9 48 83 C1 ?? 48 8D 55` (at 6) | `0x28` |
| The lookup of the object (`0x00162030`) | `CALL` at `0x0022732B`, after the `ADD` | `48 8D 55 77 E8 ..` | `48 8B D9 48 83 C1 ?? 48 8D 55 ?? E8 ?? ?? ?? ??` (rel32 at 12, end 16) | target `0x00162030` |
| Message of a playlist: the size of its bytes (i32) | `MOV R8,[RBX+0x50]` (`0x00227342`), the size for the parse of the message | `4C 8B 43 50 48 8B 53 48 48 8D 4D` | `4C 8B 43 ?? 48 8B 53 ?? 48 8D 4D` (at 3) | `0x50` |
| Message of a playlist: the pointer to its bytes | `MOV RDX,[RBX+0x48]` (`0x00227346`) | same | same pattern, at 7 | `0x48` |
| The object id in the key | `MOV RDX,[RDI+0x8]` (`0x0016207B`), before the call of the manager | `48 8B 57 08 FF 50 18` | `48 8B 57 ?? FF 50` (at 3), window `0x69` | `0x8` |
| Channel: the key of the current clip (16 bytes) | `LEA RDX,[RBX+0xA0]` (`0x017C4E4C`), the argument of the open of the clip | `48 8D 93 A0 00 00 00 48 8B CB E8` | `48 8D 93 ?? ?? ?? ?? 48 8B CB E8` (at 3) | `0xA0` |
| Listener state: the copy of the camera matrix (4x4 float) | `SUBPS XMM1,[RSI+0x40]` (`0x01A789E0` + 8), after `MOVUPS XMM1,[R14+0xF0]` | `41 0F 10 8E F0 00 00 00 0F 5C 4E 40` | `41 0F 10 8E ?? ?? ?? ?? 0F 5C 4E ??` (at 11) | `0x40` |

The first pattern of the listener (`0F 11 46 ?? 41 0F 10 8E`, the copy at `0x01A78A81`) matched two places; the
compare at `0x01A789E0` matches one.

The object id of a playlist message is at op +0x30: the key of the object at +0x28, and the id 8 bytes into it. The
first version read op +0x28 and got another field of the key (for example `0x7ff400000001`); the third game test
(`D:\sims_4\runs\2026-10-25-tv-third\notes.md`) showed it. Thus the playlist never linked the channel of our TV.

### The bytes of the message

`HandleVideoSetPlaylist` gives op +0x48 (RDX) and the low 32 bits of op +0x50 (R8D) to `0x00684FC0` ->
`0x0048AF60` -> `0x0048AE60`. That is protobuf `MessageLite::ParseFromArray(data, int size)`: `0x0048AE60` makes a
`CodedInputStream` with the start `data` and the end `data + (i64)size` (`MOVSXD RAX,EDX; ADD RAX,RCX`). So op +0x48
is the pointer to the serialized `VideoSetPlaylist`, and op +0x50 its size. The before-callback reads these bytes
before the game parses them. The field numbers come from the descriptors in the game Python
(`generated/protocolbuffers/DistributorOps_pb2.py`, `ResourceKey_pb2.py`): `VideoSetPlaylist.clip_keys` = 2
(repeated `ResourceKey`); `ResourceKey.type` = 1 (uint32), `group` = 2 (uint32), `instance` = 3 (uint64).

### The clip key

The key is 16 bytes: u64 instance, u32 type, u32 group. `HandleVideoSetPlaylist` builds it in this order from the
protobuf `ResourceKey` (`0x0022741B` .. `0x0022742D`: the instance at key +0, the field at message +0x10 at key +8,
the field at message +0x14 at key +0xC). The protobuf declares type before group, so +8 is the type and +0xC the
group. The mod compares the full 16 bytes with the key that the script mod gives. The first game test must confirm
the order (log the key of a TV channel and compare it with `video.object_info`).

### Checks of the layout

`ReadRendererLayout` refuses fields of one object that overlap (frame: three pointers and a flag; plane: two u32, a
u64 and a pointer; renderer: two u32 and a pointer).

## Not read from code

- The size of the plane allocation. The mod writes only `height` rows of `width` bytes with the stride `pitch`, and
  checks that `pitch >= width`, that the sizes are 16 to 4096, and that U and V have half the size of Y.
- The camera itself (camera +0x60 position, +0xF0 matrix): the listener callback does not get the camera. The mod
  reads the copy of the matrix in the listener state after the original function ran.

## The arguments of each hooked function (2026-10-25, lane `mods/no-setplaylist-hook`)

The hook manager of `common` (`src/mod_game/Hooks.cpp`, `HookEntry`) gives the original function only the four
register arguments (RCX, RDX, R8, R9). A function that reads an argument from the stack, or a float argument in
XMM0..XMM3, gets other values through the hook. The check below reads the disassembly in the catalogue: the frame
size of the prologue, each read of `[RSP+x]` (or of `[RBP+x]`, `[RAX+x]` with the base from the prologue) at an
offset of entry RSP +0x28 or more (the fifth argument and up), and the first use of RCX, RDX, R8, R9 and XMM0..XMM3
before the first call. The script: `argscan.py` (a scratch tool of the lane, the method is in this paragraph).

| Function | RVA | Frame at entry | Register arguments that the body reads | Stack arguments (entry RSP +) | Float arguments | Safe for the hook manager |
|---|---|---|---|---|---|---|
| `VideoRenderer::RenderFrame` | `0x017C7280` | `0xE8` (`MOV RAX,RSP`, 5 pushes, `SUB RSP,0xC0`) | RCX renderer, RDX frame | none (the `[RAX+0x8/0x10/0x20]` stores are the home slots) | none (XMM0..3 not read before the first call) | yes (2 arguments) |
| `HandleVideoSetPlaylist` | `0x00227300` | `0xE8` (3 pushes, `SUB RSP,0xD0`; RBP = entry -0x5F) | RCX op | none (the highest is `[RBP+0x77]` = entry +0x18, a home slot used as a local) | none | yes (1 argument) |
| `VideoComponent::SetPlaylist` | `0x017C3210` | `0x48` (3 pushes, `SUB RSP,0x30`) | RCX component, DX version, R8 clip keys, R9D count | **3**: `[RSP+0x70]` = +0x28 (u8, `final_loop`, read in the loop of the clips), `[RSP+0x78]` = +0x30 (u8, `video_overlay`, to component +0x41 and the fixup), `[RSP+0x80]` = +0x38 (u8, `append_clip`: append or clear) | none | **no** (7 arguments) |
| `UpdateListenerFromCamera` | `0x01A78940` | `0x238` (5 pushes, `SUB RSP,0x210`) | RCX listener state, R8 sound interface, R9B flag (RDX is not read) | none | none (XMM0..3 are written before they are read; the SSE work uses the camera and the state) | yes (4 arguments) |
| Extender tick: `UpdateServices` | `0x01637D20` | `0x28` (1 push, `SUB RSP,0x20`) | RCX services, RDX clock, R8B, R9B (two flags) | none | none | yes (4 arguments) |
| Extender quit hook | `0x0006E080` | `0x28` (`SUB RSP,0x28`) | RCX application | none (`[RSP+0x30]`, `[RSP+0x38]` = entry +0x8, +0x10, home slots) | none | yes (1 argument) |

`HandleVideoSetPlaylist` calls `SetPlaylist` with the three bytes on the stack (`0x002274AF` .. `0x002274BF`:
message +0x19, +0x18, +0x17 to `[RSP+0x30]`, `[RSP+0x28]`, `[RSP+0x20]` of the caller, that is entry +0x38, +0x30,
+0x28 of the callee). Through the hook, these three
bytes came from the frame of `HookEntry`. Thus the game got a random loop flag, overlay flag and append flag for each
list of clips of each TV: the probable cause of the black screen of every TV, also of the game's own News, in the
fourth game test.

**The fix in the mod** (no change in `common` or `mod_loader`): the mod has no hook on `SetPlaylist`. It links a TV
to its channel by the clip:

1. The before-callback on `HandleVideoSetPlaylist` reads the object id (op +0x30) and the clip keys from the bytes of
   the message (op +0x48, +0x50), and keeps the newest list of each object.
2. The before-callback on `RenderFrame` reads the channel (renderer +0xA0) and its current clip key (channel +0xA0).
   If the object of the channel does not have that clip in its newest list, the channel goes to the object whose
   newest list has the clip and that no channel showed yet. An object with no channel comes first, then the newest
   list (`SessionManager::LinkChannelByClip`).
3. The time rule, for the TVs: when a channel starts a clip that is in no list, the channel goes to the object whose
   newest list no channel showed yet and came in the last 1.5 s (the newest list first).

Rule 3 is necessary. Game start 1 of the fifth test (`D:\sims_4\runs\2026-10-25-tv-fifth\notes.md`) showed that
the channel of a TV does not play the key of its list. The playlist of our TV had one key of the type `0x545AC67A`
(`efe29ab011f5411a:545ac67a:00bf458b`), and 16 ms later a new channel showed its first frame with a VP6 key
(`ea6fc4038e0f1ec3:376840d7:00000000`) that no message had. The code of the channel does not explain the change:
`SetPlaylist` gives each key to the channel (slot +0x58, `0x017C36D0`, a deque of 24-byte entries at channel
+0xB8..+0x108), and the open of a clip (`0x017C43C0`) copies the entry that it opens into channel +0xA0. The other
video objects (lists with a `0x376840D7` key) linked by the same clip. Open point: where the client changes a
`0x545AC67A` key into a VP6 clip (a different path than `SetPlaylist`, or a resource that lists clips).

The other two callbacks of the mod (`RenderFrame`, `UpdateListenerFromCamera`) and the two hooks of the extender are
safe with the hook manager as it is now.
