/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>

#include <array>
#include <span>

#include <common/Result.h>
#include <core/CodeOffsets.h>
#include <mod_core/Signature.h>

/// The native functions of the video of a TV that the mod uses, and the field offsets that the mod reads from their
/// code. Each function has a named signature. Each offset has a rule that finds one instruction in the code of that
/// function (see `CodeOffsets.h`). If one signature or one rule fails, the mod turns off only the parts that need it.
namespace ts4_mod::youtube_tv::game_layout
{

/// The function that copies the three planes of one decoded video frame into the textures of the screen. It gets the
/// video renderer and the frame.
inline constexpr auto k_RenderFrameSignature =
    MakeSignature("48 8B C4 57 41 54 41 55 41 56 41 57 48 81 EC C0 00 00 00 48 C7 40 80 FE FF FF FF");
/// The function that gives a list of clips to the video component of an object. It gets the component, the version
/// of the list, the clip keys and the count of the keys.
inline constexpr auto k_SetPlaylistSignature = MakeSignature("48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 30 48 8B D9");
/// The function that applies a list of clips from the simulation to one object. It gets the message, which has the
/// object id.
inline constexpr auto k_PlaylistMessageSignature =
    MakeSignature("40 55 56 57 48 8D 6C 24 B9 48 81 EC D0 00 00 00 48 C7 45 37 FE FF FF FF");
/// The function that updates one video channel with the clock speed. The mod only reads its code.
inline constexpr auto k_ChannelUpdateSignature =
    MakeSignature("48 89 5C 24 10 48 89 74 24 18 55 57 41 56 48 8D 6C 24 B9 48 81 EC B0 00 00 00 48 8B D9");
/// The function that puts the audio listener of the game at the camera. It gets the state of the listener.
inline constexpr auto k_ListenerSignature = MakeSignature("44 88 4C 24 20 55 56 57 41 54 41 56");

/// The bytes of code after each signature match that the rules search. A window is not longer than its function, so a
/// rule searches only the code of that function. Each rule must match exactly one place in its window.
inline constexpr std::size_t k_RenderFrameWindow = 0x500;
inline constexpr std::size_t k_SetPlaylistWindow = 0x150;
inline constexpr std::size_t k_PlaylistMessageWindow = 0x200;
inline constexpr std::size_t k_ChannelUpdateWindow = 0x6E0;
inline constexpr std::size_t k_ListenerWindow = 0x200;

// The limits of the offsets. A field of these objects is never at a larger offset in this game.
inline constexpr std::int64_t k_MaxObjectOffset = 0x400;
inline constexpr std::int64_t k_PointerAlignment = 8;
inline constexpr std::int64_t k_IntAlignment = 4;

/// The pointers to the Y, U and V planes in a frame. The function reads them in order into three registers.
inline constexpr std::string_view k_FramePlanesPattern =
    "4D 8B AF ?? ?? ?? ?? 4C 89 6C 24 50 49 8B 87 ?? ?? ?? ?? 48 89 44 24 58 49 8B 87 ?? ?? ?? ?? 48 89 44 24 60";
inline constexpr DisplacementRule k_FramePlaneY {.name = "frame plane Y",
                                                 .pattern = k_FramePlanesPattern,
                                                 .at = 3,
                                                 .size = DisplacementSize::Dword,
                                                 .min = 0x10,
                                                 .max = k_MaxObjectOffset,
                                                 .alignment = k_PointerAlignment};
inline constexpr DisplacementRule k_FramePlaneU {.name = "frame plane U",
                                                 .pattern = k_FramePlanesPattern,
                                                 .at = 15,
                                                 .size = DisplacementSize::Dword,
                                                 .min = 0x10,
                                                 .max = k_MaxObjectOffset,
                                                 .alignment = k_PointerAlignment};
inline constexpr DisplacementRule k_FramePlaneV {.name = "frame plane V",
                                                 .pattern = k_FramePlanesPattern,
                                                 .at = 27,
                                                 .size = DisplacementSize::Dword,
                                                 .min = 0x10,
                                                 .max = k_MaxObjectOffset,
                                                 .alignment = k_PointerAlignment};

/// The fields of one plane, from the call that uploads the plane: the height, the width, the pitch, the data and the
/// texture.
inline constexpr std::string_view k_PlaneFieldsPattern =
    "8B 43 ?? 89 44 24 28 8B 03 89 44 24 20 4C 8B 4B ?? 4C 8B 43 ?? 33 D2 48 8B 4B ??";
inline constexpr DisplacementRule k_PlaneWidth {.name = "plane width",
                                                .pattern = k_PlaneFieldsPattern,
                                                .at = 7,
                                                .size = DisplacementSize::None,
                                                .min = 0,
                                                .max = 0,
                                                .alignment = k_IntAlignment};
inline constexpr DisplacementRule k_PlaneHeight {.name = "plane height",
                                                 .pattern = k_PlaneFieldsPattern,
                                                 .at = 2,
                                                 .size = DisplacementSize::Byte,
                                                 .min = 0,
                                                 .max = 0x40,
                                                 .alignment = k_IntAlignment};
inline constexpr DisplacementRule k_PlanePitch {.name = "plane pitch",
                                                .pattern = k_PlaneFieldsPattern,
                                                .at = 16,
                                                .size = DisplacementSize::Byte,
                                                .min = 0,
                                                .max = 0x40,
                                                .alignment = k_PointerAlignment};
inline constexpr DisplacementRule k_PlaneData {.name = "plane data",
                                               .pattern = k_PlaneFieldsPattern,
                                               .at = 20,
                                               .size = DisplacementSize::Byte,
                                               .min = 0,
                                               .max = 0x40,
                                               .alignment = k_PointerAlignment};

/// The size of the render target of the renderer: the height, then the width.
inline constexpr std::string_view k_RendererSizePattern = "44 8B 86 ?? ?? ?? ?? 8B 96 ?? ?? ?? ?? 48 8B 4E";
inline constexpr DisplacementRule k_RendererHeight {.name = "renderer height",
                                                    .pattern = k_RendererSizePattern,
                                                    .at = 3,
                                                    .size = DisplacementSize::Dword,
                                                    .min = 0x10,
                                                    .max = k_MaxObjectOffset,
                                                    .alignment = k_IntAlignment};
inline constexpr DisplacementRule k_RendererWidth {.name = "renderer width",
                                                   .pattern = k_RendererSizePattern,
                                                   .at = 9,
                                                   .size = DisplacementSize::Dword,
                                                   .min = 0x10,
                                                   .max = k_MaxObjectOffset,
                                                   .alignment = k_IntAlignment};

/// The video channel of the renderer.
inline constexpr DisplacementRule k_RendererChannel {.name = "renderer channel",
                                                     .pattern = "48 8B 8E ?? ?? ?? ?? 48 85 C9 74 ?? E8",
                                                     .at = 3,
                                                     .size = DisplacementSize::Dword,
                                                     .min = 0x10,
                                                     .max = k_MaxObjectOffset,
                                                     .alignment = k_PointerAlignment};

/// The flag of a frame that tells the renderer to show the rows in the opposite order.
inline constexpr DisplacementRule k_FrameFlip {.name = "frame flip flag",
                                               .pattern = "41 0F B6 87 ?? ?? ?? ?? 84 C0 74",
                                               .at = 4,
                                               .size = DisplacementSize::Dword,
                                               .min = 0x10,
                                               .max = k_MaxObjectOffset,
                                               .alignment = 1};

/// The video channel of a video component.
inline constexpr DisplacementRule k_ComponentChannel {.name = "component channel",
                                                      .pattern = "48 8B D9 41 8B F9 48 8B 49 ?? 49 8B",
                                                      .at = 9,
                                                      .size = DisplacementSize::Byte,
                                                      .min = 0x8,
                                                      .max = 0x78,
                                                      .alignment = k_PointerAlignment};

/// The object id in the message of a list of clips.
inline constexpr DisplacementRule k_MessageObjectId {.name = "message object id",
                                                     .pattern = "48 8B D9 48 83 C1 ?? 48 8D 55",
                                                     .at = 6,
                                                     .size = DisplacementSize::Byte,
                                                     .min = 0x8,
                                                     .max = 0x78,
                                                     .alignment = k_PointerAlignment};

/// The key of the clip that the channel plays now.
inline constexpr DisplacementRule k_ChannelClipKey {.name = "channel clip key",
                                                    .pattern = "48 8D 93 ?? ?? ?? ?? 48 8B CB E8",
                                                    .at = 3,
                                                    .size = DisplacementSize::Dword,
                                                    .min = 0x10,
                                                    .max = k_MaxObjectOffset,
                                                    .alignment = k_PointerAlignment};

/// The copy of the camera matrix in the state of the listener: four rows of four floats.
inline constexpr DisplacementRule k_ListenerCameraMatrix {.name = "listener camera matrix",
                                                          .pattern = "41 0F 10 8E ?? ?? ?? ?? 0F 5C 4E ??",
                                                          .at = 11,
                                                          .size = DisplacementSize::Byte,
                                                          .min = 0,
                                                          .max = 0x70,
                                                          .alignment = 16};

/// The offsets of the frame, of its planes and of the renderer.
struct RendererLayout
{
	std::array<std::size_t, 3> framePlanes {}; ///< Y, U, V.
	std::size_t frameFlip = 0;
	std::size_t planeWidth = 0;
	std::size_t planeHeight = 0;
	std::size_t planePitch = 0;
	std::size_t planeData = 0;
	std::size_t rendererWidth = 0;
	std::size_t rendererHeight = 0;
	std::size_t rendererChannel = 0;
};

/// Read the offsets from the code of the function that copies the planes. The result is an error if one rule fails,
/// or if the fields of one object overlap.
[[nodiscard]] ts4::Result<RendererLayout> ReadRendererLayout(std::span<const std::byte> renderFrameCode);

/// The offsets that link an object to its video channel.
struct ChannelLayout
{
	std::size_t componentChannel = 0;
	std::size_t messageObjectId = 0;
	std::size_t channelClipKey = 0;
};

[[nodiscard]] ts4::Result<ChannelLayout> ReadChannelLayout(std::span<const std::byte> setPlaylistCode,
                                                           std::span<const std::byte> messageCode,
                                                           std::span<const std::byte> channelUpdateCode);

/// The offset of the camera matrix in the state of the listener.
[[nodiscard]] ts4::Result<std::size_t> ReadListenerLayout(std::span<const std::byte> listenerCode);

} // namespace ts4_mod::youtube_tv::game_layout
