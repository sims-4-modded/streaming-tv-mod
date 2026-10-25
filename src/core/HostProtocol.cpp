/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "HostProtocol.h"

#include <cstring>

#include <atomic>
#include <new>

namespace ts4_mod::youtube_tv::host
{

std::size_t SharedBlockBytes(std::uint32_t channels, std::uint32_t capacityFrames)
{
	return sizeof(SharedBlockHeader) + AudioRing::RequiredBytes(channels, capacityFrames);
}

ts4::Result<SharedBlock> SharedBlock::Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
                                           std::uint32_t capacityFrames)
{
	const auto bytes = SharedBlockBytes(k_AudioChannels, capacityFrames);
	if (memory.size() < bytes)
	{
		return ts4::MakeError("the shared block needs {} bytes, but the memory has {}", bytes, memory.size());
	}
	if (reinterpret_cast<std::uintptr_t>(memory.data()) % alignof(SharedBlockHeader) != 0)
	{
		return ts4::MakeError("the shared memory is not aligned to {} bytes", alignof(SharedBlockHeader));
	}
	if (width < k_MinVideoSize || height < k_MinVideoSize || width > k_MaxVideoSize || height > k_MaxVideoSize)
	{
		return ts4::MakeError("the size {}x{} is not from {} to {} pixels", width, height, k_MinVideoSize, k_MaxVideoSize);
	}
	auto audio = AudioRing::Make(memory.subspan(sizeof(SharedBlockHeader)), k_AudioSampleRate, k_AudioChannels, capacityFrames);
	if (!audio)
	{
		return audio.GetError();
	}
	auto* header = new (memory.data()) SharedBlockHeader {};
	header->version = k_ProtocolVersion;
	header->totalBytes = static_cast<std::uint32_t>(bytes);
	header->audioOffset = sizeof(SharedBlockHeader);
	header->frame.width = width;
	header->frame.height = height;
	header->frame.format = k_FrameFormatBgra8;
	// The parent reads the magic first: write it last.
	std::atomic_ref(header->magic).store(k_SharedMagic, std::memory_order_release);
	return SharedBlock(header, *audio);
}

ts4::Result<SharedBlock> SharedBlock::Attach(std::span<std::byte> memory)
{
	if (memory.size() < sizeof(SharedBlockHeader))
	{
		return ts4::MakeError("the shared memory is too small for its header");
	}
	if (reinterpret_cast<std::uintptr_t>(memory.data()) % alignof(SharedBlockHeader) != 0)
	{
		return ts4::MakeError("the shared memory is not aligned to {} bytes", alignof(SharedBlockHeader));
	}
	auto* header = std::launder(reinterpret_cast<SharedBlockHeader*>(memory.data()));
	if (std::atomic_ref(header->magic).load(std::memory_order_acquire) != k_SharedMagic)
	{
		return ts4::MakeError("the shared memory does not start with the magic of the helper");
	}
	if (header->version != k_ProtocolVersion)
	{
		return ts4::MakeError("the helper uses the version {} of the protocol, not {}", header->version, k_ProtocolVersion);
	}
	if (header->totalBytes > memory.size() || header->audioOffset != sizeof(SharedBlockHeader) ||
	    header->audioOffset >= header->totalBytes)
	{
		return ts4::MakeError("the sizes in the header of the shared memory are not correct");
	}
	if (header->frame.format != k_FrameFormatBgra8)
	{
		return ts4::MakeError("the format {} of the frames is not BGRA8", header->frame.format);
	}
	auto audio = AudioRing::Attach(memory.subspan(header->audioOffset, header->totalBytes - header->audioOffset));
	if (!audio)
	{
		return audio.GetError();
	}
	return SharedBlock(header, *audio);
}

void SharedBlock::PublishFrame(std::uint64_t frameTimeUs)
{
	std::atomic_ref(_header->frame.frameTimeUs).store(frameTimeUs, std::memory_order_relaxed);
	std::atomic_ref(_header->frame.frameCounter).fetch_add(1, std::memory_order_release);
}

std::uint64_t SharedBlock::FrameCounter() const
{
	return std::atomic_ref(_header->frame.frameCounter).load(std::memory_order_acquire);
}

std::uint64_t SharedBlock::FrameTimeUs() const
{
	return std::atomic_ref(_header->frame.frameTimeUs).load(std::memory_order_relaxed);
}

const char* PlayerStateName(int state)
{
	switch (static_cast<PlayerState>(state))
	{
	case PlayerState::Unstarted:
		return "unstarted";
	case PlayerState::Ended:
		return "ended";
	case PlayerState::Playing:
		return "playing";
	case PlayerState::Paused:
		return "paused";
	case PlayerState::Buffering:
		return "buffering";
	case PlayerState::Cued:
		return "cued";
	}
	return "unknown";
}

const char* PlayerErrorText(int code)
{
	switch (code)
	{
	case 2:
		return "the request has a parameter that is not correct";
	case 5:
		return "the HTML5 player cannot play the video";
	case 100:
		return "the video is not available (removed or private)";
	case 101:
	case 150:
		return "the owner of the video does not permit embedded players";
	case 153:
		return "the player did not get the origin of the page";
	default:
		return "an unknown error of the player";
	}
}

} // namespace ts4_mod::youtube_tv::host
