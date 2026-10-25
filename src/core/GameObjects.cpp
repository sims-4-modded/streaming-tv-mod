/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "GameObjects.h"

namespace ts4_mod::youtube_tv
{

// The key of a clip in the game has a 64-bit instance, a 32-bit type and a 32-bit group, in this order.
static_assert(sizeof(ScreenKey) == 16);

std::optional<GameFrame> ReadGameFrame(const MemoryAccess& memory, std::uintptr_t frame,
                                       const game_layout::RendererLayout& layout)
{
	if (frame == 0)
	{
		return std::nullopt;
	}
	GameFrame result;
	const auto flip = ReadValue<std::uint8_t>(memory, frame + layout.frameFlip);
	if (!flip)
	{
		return std::nullopt;
	}
	result.flip = *flip != 0;
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto plane = ReadValue<std::uintptr_t>(memory, frame + layout.framePlanes[index]);
		if (!plane || *plane == 0)
		{
			return std::nullopt;
		}
		const auto width = ReadValue<std::uint32_t>(memory, *plane + layout.planeWidth);
		const auto height = ReadValue<std::uint32_t>(memory, *plane + layout.planeHeight);
		const auto pitch = ReadValue<std::uint64_t>(memory, *plane + layout.planePitch);
		const auto data = ReadValue<std::uintptr_t>(memory, *plane + layout.planeData);
		if (!width || !height || !pitch || !data || *data == 0)
		{
			return std::nullopt;
		}
		auto& out = result.planes[index];
		out.width = *width;
		out.height = *height;
		out.pitch = *pitch;
		// Check the sizes before the size of the data: a bad pitch must not give a large request.
		if (out.width == 0 || out.height == 0 || out.width > k_MaxGamePlaneSize || out.height > k_MaxGamePlaneSize ||
		    out.pitch < out.width || out.pitch > 4 * std::uint64_t {k_MaxGamePlaneSize})
		{
			return std::nullopt;
		}
		const auto bytes = static_cast<std::size_t>(out.pitch * (out.height - 1) + out.width);
		out.data = memory.Writable(*data, bytes);
		if (out.data.size() != bytes)
		{
			return std::nullopt;
		}
	}
	if (!CheckGameFrame(result))
	{
		return std::nullopt;
	}
	return result;
}

std::uintptr_t ReadRendererChannel(const MemoryAccess& memory, std::uintptr_t renderer,
                                   const game_layout::RendererLayout& layout)
{
	if (renderer == 0)
	{
		return 0;
	}
	return ReadValue<std::uintptr_t>(memory, renderer + layout.rendererChannel).value_or(0);
}

std::optional<std::array<std::uint32_t, 2>> ReadRendererSize(const MemoryAccess& memory, std::uintptr_t renderer,
                                                             const game_layout::RendererLayout& layout)
{
	if (renderer == 0)
	{
		return std::nullopt;
	}
	const auto width = ReadValue<std::uint32_t>(memory, renderer + layout.rendererWidth);
	const auto height = ReadValue<std::uint32_t>(memory, renderer + layout.rendererHeight);
	if (!width || !height)
	{
		return std::nullopt;
	}
	return std::array {*width, *height};
}

std::optional<ScreenKey> ReadChannelClipKey(const MemoryAccess& memory, std::uintptr_t channel,
                                            const game_layout::ChannelLayout& layout)
{
	if (channel == 0)
	{
		return std::nullopt;
	}
	const auto key = ReadValue<ScreenKey>(memory, channel + layout.channelClipKey);
	if (!key || key->type == 0)
	{
		return std::nullopt;
	}
	return key;
}

std::uintptr_t ReadComponentChannel(const MemoryAccess& memory, std::uintptr_t component,
                                    const game_layout::ChannelLayout& layout)
{
	if (component == 0)
	{
		return 0;
	}
	return ReadValue<std::uintptr_t>(memory, component + layout.componentChannel).value_or(0);
}

std::optional<std::uint64_t> ReadMessageObjectId(const MemoryAccess& memory, std::uintptr_t message,
                                                 const game_layout::ChannelLayout& layout)
{
	if (message == 0)
	{
		return std::nullopt;
	}
	return ReadValue<std::uint64_t>(memory, message + layout.messageObjectId);
}

std::optional<std::array<float, 16>> ReadListenerMatrix(const MemoryAccess& memory, std::uintptr_t state,
                                                        std::size_t matrixOffset)
{
	if (state == 0)
	{
		return std::nullopt;
	}
	return ReadValue<std::array<float, 16>>(memory, state + matrixOffset);
}

} // namespace ts4_mod::youtube_tv
