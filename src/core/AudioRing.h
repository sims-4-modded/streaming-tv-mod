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

namespace ts4_mod::youtube_tv
{

/// The header of a ring buffer of PCM audio in shared memory. The samples follow the header directly.
///
/// One producer (the helper) and one consumer (the DLL mod) use the ring without a lock:
/// - Only the producer writes `writeFrame`, `droppedFrames`, `sampleRate` and `streamCounter`.
/// - Only the consumer writes `readFrame` and `missingFrames`.
///
/// The positions count frames from the start and never go back. A frame is one float sample for each channel.
/// The samples are interleaved: frame `n` starts at sample `(n % capacityFrames) * channels`.
struct AudioRingHeader
{
	std::uint32_t sampleRate;     ///< The frames in one second. The producer changes it when a new stream starts.
	std::uint32_t channels;       ///< The samples in one frame. It does not change.
	std::uint32_t capacityFrames; ///< The frames that the ring can keep. It does not change.
	std::uint32_t streamCounter;  ///< The producer adds 1 when a new audio stream starts.

	/// Padding: the fields of the producer and of the consumer are on different cache lines.
	std::array<std::byte, 48> padding0;
	std::uint64_t writeFrame;    ///< The position after the last frame that the producer wrote.
	std::uint64_t droppedFrames; ///< The frames that the producer did not write because the ring was full.

	std::array<std::byte, 48> padding1;
	std::uint64_t readFrame;     ///< The position after the last frame that the consumer read.
	std::uint64_t missingFrames; ///< The frames that the consumer asked for when the ring was empty.
	std::array<std::byte, 48> padding2;
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
static_assert(sizeof(AudioRingHeader) == 192);
static_assert(offsetof(AudioRingHeader, writeFrame) == 64);
static_assert(offsetof(AudioRingHeader, readFrame) == 128);

/// The largest number of channels in one frame.
constexpr std::uint32_t k_MaxAudioChannels = 8;

/// A view of a ring buffer of PCM audio in memory that the view does not own (usually shared memory).
class AudioRing
{
public:
	/// The bytes that a ring with these values needs, with its header.
	[[nodiscard]] static std::size_t RequiredBytes(std::uint32_t channels, std::uint32_t capacityFrames);

	/// Write a new, empty ring into the memory. The result is an error if the memory is too small or a value is
	/// not correct.
	[[nodiscard]] static ts4::Result<AudioRing> Make(std::span<std::byte> memory, std::uint32_t sampleRate,
	                                                 std::uint32_t channels, std::uint32_t capacityFrames);

	/// Use a ring that a different process wrote into the memory. The result is an error if the header is not correct.
	[[nodiscard]] static ts4::Result<AudioRing> Attach(std::span<std::byte> memory);

	// The producer side.

	/// Write interleaved frames. If the ring does not have space for all frames, the ring keeps the frames that fit,
	/// and adds the others to `droppedFrames`. Returns the number of frames that the ring keeps.
	std::size_t Write(std::span<const float> interleaved);

	/// Write frames from one array for each channel. A missing channel gets the first channel; the ring ignores the
	/// channels after its own count. Returns the number of frames that the ring keeps.
	std::size_t WritePlanar(std::span<const float* const> planes, std::size_t frames);

	/// Set the sample rate of a new stream, and add 1 to the stream counter.
	void StartStream(std::uint32_t sampleRate);

	// The consumer side.

	/// Read interleaved frames into `out`. If the ring has fewer frames than `out` can hold, the function reads all
	/// of them, and adds the difference to `missingFrames`. Returns the number of frames that it read.
	std::size_t Read(std::span<float> out);

	/// Remove the oldest frames, so that a maximum of `keepFrames` frames stay. Returns the number of frames removed.
	/// The consumer uses it to keep the delay of the audio small.
	std::size_t DropOldest(std::size_t keepFrames);

	// Both sides.

	/// The frames that the producer wrote and the consumer did not read.
	[[nodiscard]] std::size_t AvailableFrames() const;
	/// The frames that the producer can write now.
	[[nodiscard]] std::size_t FreeFrames() const;

	[[nodiscard]] std::uint32_t SampleRate() const;
	[[nodiscard]] std::uint32_t Channels() const { return _header->channels; }
	[[nodiscard]] std::uint32_t CapacityFrames() const { return _header->capacityFrames; }
	[[nodiscard]] std::uint32_t StreamCounter() const;
	[[nodiscard]] std::uint64_t DroppedFrames() const;
	[[nodiscard]] std::uint64_t MissingFrames() const;

private:
	AudioRing(AudioRingHeader* header, float* samples)
	    : _header(header)
	    , _samples(samples)
	{
	}

	/// Copy one frame position of the ring to the index of its first sample.
	[[nodiscard]] std::size_t SampleIndex(std::uint64_t frame) const;

	AudioRingHeader* _header;
	float* _samples;
};

} // namespace ts4_mod::youtube_tv
