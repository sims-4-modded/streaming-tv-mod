/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <atomic>
#include <span>

#include <common/Result.h>
#include <core/AudioRing.h>
#include <core/Yuv.h>

/// The shared parts of the helper `youtube_tv_host.exe` and of its parent (the DLL mod, or the probe). Both sides
/// include this header, and it uses no Windows type.
///
/// For each video, the helper makes one block of shared memory and copies its handle into the parent process. The
/// block has these parts, in this order:
/// 1. A `SharedBlockHeader`.
/// 2. The ring of PCM audio (`AudioRing`).
/// 3. The frames: a `FrameBufferHeader`, then three slots, each with a `FrameSlotHeader` and the I420 planes Y, U, V.
namespace ts4_mod::youtube_tv::host
{

/// The version of the commands, the events and the shared memory. The helper sends it in the `hello` event.
constexpr std::uint32_t k_ProtocolVersion = 2;

/// The first four bytes of the shared memory: "YTVH" in little-endian order.
constexpr std::uint32_t k_SharedMagic = 0x48565459;

/// The format of the audio that the helper writes: stereo float at 48000 frames each second.
constexpr std::uint32_t k_AudioChannels = 2;
constexpr std::uint32_t k_AudioSampleRate = 48000;
/// The ring keeps two seconds of audio.
constexpr std::uint32_t k_AudioCapacityFrames = k_AudioSampleRate * 2;

/// The limits of the size of a video, in pixels. The width and the height are multiples of `k_VideoSizeStep`.
constexpr std::uint32_t k_VideoSizeStep = 16;
constexpr std::uint32_t k_MinVideoSize = 16;
constexpr std::uint32_t k_MaxVideoSize = 4096;

/// The limits of the playback rate of the YouTube player.
constexpr double k_MinPlaybackRate = 0.25;
constexpr double k_MaxPlaybackRate = 2.0;

/// The number of frame slots. The producer writes one slot, the consumer reads one slot, and the third slot keeps the
/// newest complete frame.
constexpr std::uint32_t k_FrameSlots = 3;

/// The bit of `FrameBufferHeader::exchange` that tells that the slot in the exchange has a frame that the consumer did
/// not take. The two low bits are the index of that slot.
constexpr std::uint32_t k_NewFrameBit = 4;
constexpr std::uint32_t k_SlotIndexMask = 3;

/// Round a size up to the next multiple of `k_VideoSizeStep`, within the limits.
[[nodiscard]] std::uint32_t RoundVideoSize(std::uint32_t size);

/// The start of the shared memory of one video.
struct SharedBlockHeader
{
	std::uint32_t magic;        ///< `k_SharedMagic`.
	std::uint32_t version;      ///< `k_ProtocolVersion`.
	std::uint32_t totalBytes;   ///< The size of the full block.
	std::uint32_t audioOffset;  ///< The offset of the `AudioRingHeader` from the start of the block.
	std::uint32_t framesOffset; ///< The offset of the `FrameBufferHeader` from the start of the block.
	std::array<std::byte, 44> reserved;
};

/// The header of the frames. The helper (the producer) writes the sizes once. Each side writes only its own fields.
///
/// The rules of the slots (a lock-free triple buffer):
/// - The producer writes the planes of the slot `producerIndex`. Then it puts that index, with `k_NewFrameBit`, into
///   `exchange` with one atomic exchange, and takes the old index of `exchange` as its new `producerIndex`.
/// - The consumer reads the slot `consumerIndex`. When `exchange` has `k_NewFrameBit`, the consumer puts its index
///   (without the bit) into `exchange` with one atomic exchange, and takes the old index as its new `consumerIndex`.
/// - The three indexes are always 0, 1 and 2 in some order. Thus the producer never writes the slot of the consumer,
///   and the consumer always reads a complete frame. No side waits for the other side.
struct FrameBufferHeader
{
	std::uint32_t width;       ///< A multiple of 16.
	std::uint32_t height;      ///< A multiple of 16.
	std::uint32_t pitchY;      ///< The bytes of one row of Y.
	std::uint32_t pitchUV;     ///< The bytes of one row of U or of V.
	std::uint32_t offsetY;     ///< The offset of Y from the start of a slot.
	std::uint32_t offsetU;     ///< The offset of U from the start of a slot.
	std::uint32_t offsetV;     ///< The offset of V from the start of a slot.
	std::uint32_t slotBytes;   ///< The size of one slot, with its `FrameSlotHeader`.
	std::uint32_t slotsOffset; ///< The offset of the first slot from the start of this header.
	std::uint32_t matrix;      ///< A `YuvMatrix`.
	std::uint32_t range;       ///< A `YuvRange`.
	std::uint32_t reserved0;
	std::uint64_t frameCounter; ///< The number of frames that the producer completed.
	std::uint64_t reserved1;

	std::uint32_t exchange;      ///< The slot of the newest complete frame, and `k_NewFrameBit`.
	std::uint32_t producerIndex; ///< The slot that the producer writes. Only the producer writes this field.
	std::array<std::byte, 56> padding0;

	std::uint32_t consumerIndex; ///< The slot that the consumer reads. Only the consumer writes this field.
	std::array<std::byte, 60> padding1;
};

/// The start of each slot.
struct FrameSlotHeader
{
	std::uint64_t frameNumber; ///< The value of `frameCounter` for this frame (1 for the first frame).
	std::uint64_t timeUs;      ///< The time of the paint, in microseconds from the start of the helper.
	std::array<std::byte, 48> reserved;
};

static_assert(sizeof(SharedBlockHeader) == 64);
static_assert(sizeof(FrameBufferHeader) == 192);
static_assert(offsetof(FrameBufferHeader, exchange) == 64);
static_assert(offsetof(FrameBufferHeader, consumerIndex) == 128);
static_assert(sizeof(FrameSlotHeader) == 64);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);

/// A view of the frames of one shared block. It does not own the memory.
class FrameBuffers
{
public:
	/// The bytes of the frames of this size, with the header.
	[[nodiscard]] static std::size_t RequiredBytes(std::uint32_t width, std::uint32_t height);

	/// Write a new header and three black frames into the memory.
	[[nodiscard]] static ts4::Result<FrameBuffers> Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
	                                                    YuvMatrix matrix, YuvRange range);

	/// Use frames that the helper wrote. The result is an error if the header is not correct.
	[[nodiscard]] static ts4::Result<FrameBuffers> Attach(std::span<std::byte> memory);

	// The producer side.

	/// The planes of the slot that the producer writes now.
	[[nodiscard]] I420Planes BackPlanes();
	/// Make the slot of the producer the newest complete frame, and take a different slot to write.
	void Publish(std::uint64_t timeUs);

	// The consumer side.

	/// If the producer completed a frame that the consumer did not take, make it the slot of the consumer and return
	/// true. Otherwise the consumer keeps its slot, and the result is false.
	bool TakeNewest();
	/// The planes of the slot of the consumer.
	[[nodiscard]] ConstI420Planes FrontPlanes() const;
	/// The frame number of the slot of the consumer (0 before the first frame).
	[[nodiscard]] std::uint64_t FrontFrameNumber() const;
	[[nodiscard]] std::uint64_t FrontTimeUs() const;

	// Both sides.

	[[nodiscard]] std::uint64_t FrameCounter() const;
	[[nodiscard]] std::uint32_t Width() const { return _header->width; }
	[[nodiscard]] std::uint32_t Height() const { return _header->height; }
	[[nodiscard]] YuvMatrix Matrix() const { return static_cast<YuvMatrix>(_header->matrix); }
	[[nodiscard]] YuvRange Range() const { return static_cast<YuvRange>(_header->range); }
	/// The slot indexes of the producer, of the exchange and of the consumer. The tests use it.
	[[nodiscard]] std::array<std::uint32_t, 3> SlotIndexes() const;

private:
	explicit FrameBuffers(FrameBufferHeader* header)
	    : _header(header)
	{
	}

	[[nodiscard]] std::byte* Slot(std::uint32_t index) const;
	[[nodiscard]] FrameSlotHeader& SlotHeader(std::uint32_t index) const;

	FrameBufferHeader* _header;
};

/// The bytes of a shared block for frames of this size and an audio ring of this capacity.
[[nodiscard]] std::size_t SharedBlockBytes(std::uint32_t width, std::uint32_t height,
                                           std::uint32_t capacityFrames = k_AudioCapacityFrames);

/// A view of one shared block. It does not own the memory.
class SharedBlock
{
public:
	/// Write a new block into the memory: the header, an empty audio ring and three black frames. The width and the
	/// height must be multiples of 16 (see `RoundVideoSize`).
	[[nodiscard]] static ts4::Result<SharedBlock> Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
	                                                   YuvMatrix matrix, YuvRange range,
	                                                   std::uint32_t capacityFrames = k_AudioCapacityFrames);

	/// Use a block that the helper wrote. The result is an error if the header or the version is not correct.
	[[nodiscard]] static ts4::Result<SharedBlock> Attach(std::span<std::byte> memory);

	[[nodiscard]] AudioRing& Audio() { return _audio; }
	[[nodiscard]] FrameBuffers& Frames() { return _frames; }

private:
	SharedBlock(AudioRing audio, FrameBuffers frames)
	    : _audio(audio)
	    , _frames(frames)
	{
	}

	AudioRing _audio;
	FrameBuffers _frames;
};

/// The states of the YouTube player (the values of `YT.PlayerState`).
enum class PlayerState : int
{
	Unstarted = -1,
	Ended = 0,
	Playing = 1,
	Paused = 2,
	Buffering = 3,
	Cued = 5,
};

/// The name of a player state in the `state` event, or "unknown" for a different value.
[[nodiscard]] const char* PlayerStateName(int state);

/// The text of an error code of the YouTube player (the `onError` event).
[[nodiscard]] const char* PlayerErrorText(int code);

} // namespace ts4_mod::youtube_tv::host
