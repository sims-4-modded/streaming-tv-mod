/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "HostProtocol.h"

#include <cstring>

#include <algorithm>
#include <atomic>
#include <new>

namespace ts4_mod::youtube_tv::host
{
namespace
{

constexpr std::size_t k_Alignment = 64;

[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value)
{
	return (value + k_Alignment - 1) / k_Alignment * k_Alignment;
}

[[nodiscard]] bool IsAligned(const void* pointer)
{
	return reinterpret_cast<std::uintptr_t>(pointer) % k_Alignment == 0;
}

[[nodiscard]] bool IsVideoSize(std::uint32_t size)
{
	return size >= k_MinVideoSize && size <= k_MaxVideoSize && size % k_VideoSizeStep == 0;
}

/// The values of Y, U and V for black.
[[nodiscard]] std::uint8_t BlackY(YuvRange range)
{
	return range == YuvRange::Full ? 0 : 16;
}

constexpr std::uint8_t k_NeutralChroma = 128;

} // namespace

std::uint32_t RoundVideoSize(std::uint32_t size)
{
	const auto rounded = (std::uint64_t {size} + k_VideoSizeStep - 1) / k_VideoSizeStep * k_VideoSizeStep;
	return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounded, k_MinVideoSize, k_MaxVideoSize));
}

std::size_t FrameBuffers::RequiredBytes(std::uint32_t width, std::uint32_t height)
{
	const std::size_t ySize = std::size_t {width} * height;
	const std::size_t cSize = ySize / 4;
	const std::size_t slot = sizeof(FrameSlotHeader) + AlignUp(ySize) + 2 * AlignUp(cSize);
	return sizeof(FrameBufferHeader) + k_FrameSlots * AlignUp(slot);
}

ts4::Result<FrameBuffers> FrameBuffers::Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
                                             YuvMatrix matrix, YuvRange range)
{
	if (!IsVideoSize(width) || !IsVideoSize(height))
	{
		return ts4::MakeError("the size {}x{} is not a multiple of {} from {} to {} pixels", width, height, k_VideoSizeStep,
		                      k_MinVideoSize, k_MaxVideoSize);
	}
	if (!IsAligned(memory.data()))
	{
		return ts4::MakeError("the memory of the frames is not aligned to {} bytes", k_Alignment);
	}
	const auto bytes = RequiredBytes(width, height);
	if (memory.size() < bytes)
	{
		return ts4::MakeError("the frames need {} bytes, but the memory has {}", bytes, memory.size());
	}

	auto* header = new (memory.data()) FrameBufferHeader {};
	const std::size_t ySize = std::size_t {width} * height;
	header->width = width;
	header->height = height;
	header->pitchY = width;
	header->pitchUV = width / 2;
	header->offsetY = sizeof(FrameSlotHeader);
	header->offsetU = static_cast<std::uint32_t>(header->offsetY + AlignUp(ySize));
	header->offsetV = static_cast<std::uint32_t>(header->offsetU + AlignUp(ySize / 4));
	header->slotBytes = static_cast<std::uint32_t>(AlignUp(header->offsetV + AlignUp(ySize / 4)));
	header->slotsOffset = sizeof(FrameBufferHeader);
	header->matrix = static_cast<std::uint32_t>(matrix);
	header->range = static_cast<std::uint32_t>(range);
	header->producerIndex = 0;
	header->exchange = 1;
	header->consumerIndex = 2;

	FrameBuffers frames(header);
	for (std::uint32_t index = 0; index < k_FrameSlots; ++index)
	{
		std::byte* slot = frames.Slot(index);
		new (slot) FrameSlotHeader {};
		std::memset(slot + header->offsetY, BlackY(range), ySize);
		std::memset(slot + header->offsetU, k_NeutralChroma, ySize / 4);
		std::memset(slot + header->offsetV, k_NeutralChroma, ySize / 4);
	}
	std::atomic_thread_fence(std::memory_order_release);
	return frames;
}

ts4::Result<FrameBuffers> FrameBuffers::Attach(std::span<std::byte> memory)
{
	if (memory.size() < sizeof(FrameBufferHeader) || !IsAligned(memory.data()))
	{
		return ts4::MakeError("the memory of the frames is too small or not aligned");
	}
	auto* header = std::launder(reinterpret_cast<FrameBufferHeader*>(memory.data()));
	if (!IsVideoSize(header->width) || !IsVideoSize(header->height) || header->pitchY < header->width ||
	    header->pitchUV < header->width / 2 || header->slotsOffset != sizeof(FrameBufferHeader))
	{
		return ts4::MakeError("the header of the frames is not correct");
	}
	const std::size_t ySize = std::size_t {header->pitchY} * header->height;
	const std::size_t cSize = std::size_t {header->pitchUV} * (header->height / 2);
	if (header->offsetY < sizeof(FrameSlotHeader) || header->offsetU < header->offsetY + ySize ||
	    header->offsetV < header->offsetU + cSize || header->slotBytes < header->offsetV + cSize ||
	    memory.size() < header->slotsOffset + std::size_t {k_FrameSlots} * header->slotBytes)
	{
		return ts4::MakeError("the planes of the frames are larger than the memory");
	}
	if (header->matrix > static_cast<std::uint32_t>(YuvMatrix::Bt709) ||
	    header->range > static_cast<std::uint32_t>(YuvRange::Full) || header->consumerIndex >= k_FrameSlots)
	{
		return ts4::MakeError("the header of the frames has a value that is not correct");
	}
	return FrameBuffers(header);
}

std::byte* FrameBuffers::Slot(std::uint32_t index) const
{
	return reinterpret_cast<std::byte*>(_header) + _header->slotsOffset + std::size_t {index} * _header->slotBytes;
}

FrameSlotHeader& FrameBuffers::SlotHeader(std::uint32_t index) const
{
	return *std::launder(reinterpret_cast<FrameSlotHeader*>(Slot(index)));
}

I420Planes FrameBuffers::BackPlanes()
{
	std::byte* slot = Slot(_header->producerIndex);
	const std::size_t ySize = std::size_t {_header->pitchY} * _header->height;
	const std::size_t cSize = std::size_t {_header->pitchUV} * (_header->height / 2);
	return {
	    .y = {reinterpret_cast<std::uint8_t*>(slot + _header->offsetY), ySize},
	    .u = {reinterpret_cast<std::uint8_t*>(slot + _header->offsetU), cSize},
	    .v = {reinterpret_cast<std::uint8_t*>(slot + _header->offsetV), cSize},
	    .pitchY = _header->pitchY,
	    .pitchUV = _header->pitchUV,
	    .width = _header->width,
	    .height = _header->height,
	};
}

void FrameBuffers::Publish(std::uint64_t timeUs)
{
	const auto number = std::atomic_ref(_header->frameCounter).load(std::memory_order_relaxed) + 1;
	auto& slot = SlotHeader(_header->producerIndex);
	slot.frameNumber = number;
	slot.timeUs = timeUs;
	const auto old =
	    std::atomic_ref(_header->exchange).exchange(_header->producerIndex | k_NewFrameBit, std::memory_order_acq_rel);
	std::atomic_ref(_header->producerIndex).store(old & k_SlotIndexMask, std::memory_order_relaxed);
	std::atomic_ref(_header->frameCounter).store(number, std::memory_order_release);
}

bool FrameBuffers::TakeNewest()
{
	if ((std::atomic_ref(_header->exchange).load(std::memory_order_acquire) & k_NewFrameBit) == 0)
	{
		return false;
	}
	const auto old = std::atomic_ref(_header->exchange).exchange(_header->consumerIndex, std::memory_order_acq_rel);
	std::atomic_ref(_header->consumerIndex).store(old & k_SlotIndexMask, std::memory_order_relaxed);
	return true;
}

ConstI420Planes FrameBuffers::FrontPlanes() const
{
	const std::byte* slot = Slot(_header->consumerIndex);
	const std::size_t ySize = std::size_t {_header->pitchY} * _header->height;
	const std::size_t cSize = std::size_t {_header->pitchUV} * (_header->height / 2);
	return {
	    .y = {reinterpret_cast<const std::uint8_t*>(slot + _header->offsetY), ySize},
	    .u = {reinterpret_cast<const std::uint8_t*>(slot + _header->offsetU), cSize},
	    .v = {reinterpret_cast<const std::uint8_t*>(slot + _header->offsetV), cSize},
	    .pitchY = _header->pitchY,
	    .pitchUV = _header->pitchUV,
	    .width = _header->width,
	    .height = _header->height,
	};
}

std::uint64_t FrameBuffers::FrontFrameNumber() const
{
	return SlotHeader(_header->consumerIndex).frameNumber;
}

std::uint64_t FrameBuffers::FrontTimeUs() const
{
	return SlotHeader(_header->consumerIndex).timeUs;
}

std::uint64_t FrameBuffers::FrameCounter() const
{
	return std::atomic_ref(_header->frameCounter).load(std::memory_order_acquire);
}

std::array<std::uint32_t, 3> FrameBuffers::SlotIndexes() const
{
	return {std::atomic_ref(_header->producerIndex).load(std::memory_order_relaxed),
	        std::atomic_ref(_header->exchange).load(std::memory_order_acquire) & k_SlotIndexMask,
	        std::atomic_ref(_header->consumerIndex).load(std::memory_order_relaxed)};
}

std::size_t SharedBlockBytes(std::uint32_t width, std::uint32_t height, std::uint32_t capacityFrames)
{
	return sizeof(SharedBlockHeader) + AlignUp(AudioRing::RequiredBytes(k_AudioChannels, capacityFrames)) +
	       FrameBuffers::RequiredBytes(width, height);
}

ts4::Result<SharedBlock> SharedBlock::Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
                                           YuvMatrix matrix, YuvRange range, std::uint32_t capacityFrames)
{
	const auto bytes = SharedBlockBytes(width, height, capacityFrames);
	if (memory.size() < bytes)
	{
		return ts4::MakeError("the shared block needs {} bytes, but the memory has {}", bytes, memory.size());
	}
	if (!IsAligned(memory.data()))
	{
		return ts4::MakeError("the shared memory is not aligned to {} bytes", k_Alignment);
	}
	const auto audioOffset = sizeof(SharedBlockHeader);
	const auto framesOffset = audioOffset + AlignUp(AudioRing::RequiredBytes(k_AudioChannels, capacityFrames));
	auto audio = AudioRing::Make(memory.subspan(audioOffset, framesOffset - audioOffset), k_AudioSampleRate, k_AudioChannels,
	                             capacityFrames);
	if (!audio)
	{
		return audio.GetError();
	}
	auto frames = FrameBuffers::Make(memory.subspan(framesOffset), width, height, matrix, range);
	if (!frames)
	{
		return frames.GetError();
	}
	auto* header = new (memory.data()) SharedBlockHeader {};
	header->version = k_ProtocolVersion;
	header->totalBytes = static_cast<std::uint32_t>(bytes);
	header->audioOffset = static_cast<std::uint32_t>(audioOffset);
	header->framesOffset = static_cast<std::uint32_t>(framesOffset);
	// The parent reads the magic first: write it last.
	std::atomic_ref(header->magic).store(k_SharedMagic, std::memory_order_release);
	return SharedBlock(*audio, *frames);
}

ts4::Result<SharedBlock> SharedBlock::Attach(std::span<std::byte> memory)
{
	if (memory.size() < sizeof(SharedBlockHeader) || !IsAligned(memory.data()))
	{
		return ts4::MakeError("the shared memory is too small for its header, or not aligned");
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
	    header->framesOffset <= header->audioOffset || header->framesOffset >= header->totalBytes ||
	    header->framesOffset % k_Alignment != 0)
	{
		return ts4::MakeError("the sizes in the header of the shared memory are not correct");
	}
	auto audio = AudioRing::Attach(memory.subspan(header->audioOffset, header->framesOffset - header->audioOffset));
	if (!audio)
	{
		return audio.GetError();
	}
	auto frames = FrameBuffers::Attach(memory.subspan(header->framesOffset, header->totalBytes - header->framesOffset));
	if (!frames)
	{
		return frames.GetError();
	}
	return SharedBlock(*audio, *frames);
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
