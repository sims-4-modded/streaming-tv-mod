/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <array>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <common/Result.h>
#include <core/FrameWriter.h>
#include <core/Sessions.h>
#include <core/VideoLayout.h>

/// The reads of the video objects of the game: the frame and its planes, the channel of a renderer, the clip key of
/// a channel, the object id and the clip keys of a message and the camera matrix of the listener. Each read goes through
/// `MemoryAccess`. In the game, the access checks each page before it reads. In the tests, the access is a buffer.
namespace ts4_mod::youtube_tv
{

/// Access to the memory of the process.
class MemoryAccess
{
public:
	virtual ~MemoryAccess() = default;

	/// Copy `out.size()` bytes at `address` into `out`. Return false, and copy nothing, if a byte cannot be read.
	[[nodiscard]] virtual bool Read(std::uintptr_t address, std::span<std::byte> out) const = 0;
	/// The bytes at `address` that the mod can write, or an empty span if a byte cannot be written.
	[[nodiscard]] virtual std::span<std::uint8_t> Writable(std::uintptr_t address, std::size_t size) const = 0;
};

/// Read one value of a plain type.
template <typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::optional<T> ReadValue(const MemoryAccess& memory, std::uintptr_t address)
{
	std::array<std::byte, sizeof(T)> bytes {};
	if (address == 0 || !memory.Read(address, bytes))
	{
		return std::nullopt;
	}
	T value;
	std::memcpy(&value, bytes.data(), sizeof(T));
	return value;
}

/// Read the three planes of a frame of the game. The result is empty if a read fails, or if the planes are not
/// correct (see `CheckGameFrame`).
[[nodiscard]] std::optional<GameFrame> ReadGameFrame(const MemoryAccess& memory, std::uintptr_t frame,
                                                     const game_layout::RendererLayout& layout);

/// The channel of a renderer, or 0.
[[nodiscard]] std::uintptr_t ReadRendererChannel(const MemoryAccess& memory, std::uintptr_t renderer,
                                                 const game_layout::RendererLayout& layout);

/// The size of the render target of a renderer: the width and the height.
[[nodiscard]] std::optional<std::array<std::uint32_t, 2>> ReadRendererSize(const MemoryAccess& memory, std::uintptr_t renderer,
                                                                           const game_layout::RendererLayout& layout);

/// The key of the clip that the channel plays now. The result is empty if the read fails or the channel has no clip.
[[nodiscard]] std::optional<ScreenKey> ReadChannelClipKey(const MemoryAccess& memory, std::uintptr_t channel,
                                                          const game_layout::ChannelLayout& layout);

/// The object id in the message of a list of clips.
[[nodiscard]] std::optional<std::uint64_t> ReadMessageObjectId(const MemoryAccess& memory, std::uintptr_t message,
                                                               const game_layout::ChannelLayout& layout);

/// The clip keys in the message of a list of clips, in their order. The result is an error if a read fails or if the
/// bytes are not a correct message.
[[nodiscard]] ts4::Result<std::vector<ScreenKey>> ReadMessageClipKeys(const MemoryAccess& memory, std::uintptr_t message,
                                                                      const game_layout::ChannelLayout& layout);

/// The copy of the camera matrix in the state of the listener.
[[nodiscard]] std::optional<std::array<float, 16>> ReadListenerMatrix(const MemoryAccess& memory, std::uintptr_t state,
                                                                      std::size_t matrixOffset);

} // namespace ts4_mod::youtube_tv
