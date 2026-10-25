# The field offsets that the DLL mod reads from the code of the game

Lane `tv-dll`, 2026-10-25. Tree of every RVA in this note: **`TS4_x64_1.128.90.1030`** (the decrypted exe,
`D:\sims_4\dump\TS4_1.128.90.1030\TS4_x64.image.bin`). The catalogue entries
(`D:\sims_4uns6-10-25-tv-third
otes.md`) showed it. Thus the playlist never linked the channel of our TV.
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
| `VideoComponent::SetPlaylist` | `0x017C3210` | `0x150` (the next function starts at `0x017C3360`) |
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
| Component: its channel | `MOV RCX,[RCX+0x20]` (`0x017C3228`) | `48 8B D9 41 8B F9 48 8B 49 20 49 8B` | `48 8B D9 41 8B F9 48 8B 49 ?? 49 8B` (at 9) | `0x20` |
| Message of a playlist: the key of the object | `ADD RCX,0x28` (`0x00227323`), the argument of the lookup of the object | `48 8B D9 48 83 C1 28 48 8D 55` | `48 8B D9 48 83 C1 ?? 48 8D 55` (at 6) | `0x28` |
| The lookup of the object (`0x00162030`) | `CALL` at `0x0022732B`, after the `ADD` | `48 8D 55 77 E8 ..` | `48 8B D9 48 83 C1 ?? 48 8D 55 ?? E8 ?? ?? ?? ??` (rel32 at 12, end 16) | target `0x00162030` |
| The object id in the key | `MOV RDX,[RDI+0x8]` (`0x0016207B`), before the call of the manager | `48 8B 57 08 FF 50 18` | `48 8B 57 ?? FF 50` (at 3), window `0x69` | `0x8` |
| Channel: the key of the current clip (16 bytes) | `LEA RDX,[RBX+0xA0]` (`0x017C4E4C`), the argument of the open of the clip | `48 8D 93 A0 00 00 00 48 8B CB E8` | `48 8D 93 ?? ?? ?? ?? 48 8B CB E8` (at 3) | `0xA0` |
| Listener state: the copy of the camera matrix (4x4 float) | `SUBPS XMM1,[RSI+0x40]` (`0x01A789E0` + 8), after `MOVUPS XMM1,[R14+0xF0]` | `41 0F 10 8E F0 00 00 00 0F 5C 4E 40` | `41 0F 10 8E ?? ?? ?? ?? 0F 5C 4E ??` (at 11) | `0x40` |

The first pattern of the listener (`0F 11 46 ?? 41 0F 10 8E`, the copy at `0x01A78A81`) matched two places; the
compare at `0x01A789E0` matches one.

The object id of a playlist message is at op +0x30: the key of the object at +0x28, and the id 8 bytes into it. The
first version read op +0x28 and got another field of the key (for example `0x7ff400000001`); the third game test
(`D:\sims_4
uns6-10-25-tv-third
otes.md`) showed it. Thus the playlist never linked the channel of our TV.

## The clip key

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
