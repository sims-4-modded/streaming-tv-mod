/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <span>
#include <vector>

#include <common/Result.h>
#include <core/Sessions.h>

/// The reader of the bytes of a `VideoSetPlaylist` message (protocol buffers, `DistributorOps.proto` of the game).
/// The client gets these bytes from the simulation and reads them before it applies the list of clips. The mod reads
/// only the clip keys.
namespace ts4_mod::youtube_tv::playlist
{

/// The field numbers of the messages, from the descriptors in the game Python.
inline constexpr std::uint32_t k_ClipKeysField = 2;    ///< `VideoSetPlaylist.clip_keys`, a list of `ResourceKey`.
inline constexpr std::uint32_t k_KeyTypeField = 1;     ///< `ResourceKey.type`, a 32-bit number.
inline constexpr std::uint32_t k_KeyGroupField = 2;    ///< `ResourceKey.group`, a 32-bit number.
inline constexpr std::uint32_t k_KeyInstanceField = 3; ///< `ResourceKey.instance`, a 64-bit number.

/// The limits of a message. A list of clips of the game is much smaller.
inline constexpr std::size_t k_MaxMessageBytes = 0x10000;
inline constexpr std::size_t k_MaxClipKeys = 64;

/// Read the clip keys of the message, in their order. The result is an error if the bytes are not a correct message,
/// or if the message is larger than the limits.
[[nodiscard]] ts4::Result<std::vector<ScreenKey>> ReadClipKeys(std::span<const std::byte> message);

/// Make the bytes of a message with these clip keys and the version `version`. Only the tests and the probe use it.
[[nodiscard]] std::vector<std::byte> MakeMessage(std::uint32_t version, std::span<const ScreenKey> keys);

} // namespace ts4_mod::youtube_tv::playlist
