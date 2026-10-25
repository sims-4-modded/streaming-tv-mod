/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <span>

#include <common/Result.h>
#include <core/AudioRing.h>

/// The shared parts of the helper `youtube_tv_host.exe` and of its parent (the DLL mod, or the probe). Both sides
/// include this header, and it uses no Windows type.
///
/// For each video, the helper makes two shared objects and copies their handles into the parent process:
/// - A D3D11 texture (BGRA8) with a keyed mutex: the last frame of the video.
/// - One block of shared memory: a `SharedBlockHeader`, then the ring of PCM audio (`AudioRing`).
namespace ts4_mod::youtube_tv::host
{

/// The version of the commands, the events and the shared memory. The helper sends it in the `hello` event.
constexpr std::uint32_t k_ProtocolVersion = 1;

/// The first four bytes of the shared memory: "YTVH" in little-endian order.
constexpr std::uint32_t k_SharedMagic = 0x48565459;

/// The DXGI format of the shared texture: `DXGI_FORMAT_B8G8R8A8_UNORM`.
constexpr std::uint32_t k_FrameFormatBgra8 = 87;

/// The key of the keyed mutex of the shared texture. The two sides use the same key: each side calls
/// `AcquireSync(k_FrameMutexKey, timeout)`, uses the texture, and calls `ReleaseSync(k_FrameMutexKey)`. Thus no side
/// waits for the other side to use a frame.
constexpr std::uint64_t k_FrameMutexKey = 0;

/// The maximum time that the helper waits for the keyed mutex. If the parent keeps the mutex longer, the helper does
/// not write that frame. The parent must also keep the mutex only for one copy.
constexpr std::uint32_t k_HelperMutexTimeoutMs = 4;

/// The format of the audio that the helper writes: stereo float at 48000 frames each second.
constexpr std::uint32_t k_AudioChannels = 2;
constexpr std::uint32_t k_AudioSampleRate = 48000;
/// The ring keeps two seconds of audio.
constexpr std::uint32_t k_AudioCapacityFrames = k_AudioSampleRate * 2;

/// The limits of the size of a video, in pixels.
constexpr std::uint32_t k_MinVideoSize = 16;
constexpr std::uint32_t k_MaxVideoSize = 4096;

/// The limits of the playback rate of the YouTube player.
constexpr double k_MinPlaybackRate = 0.25;
constexpr double k_MaxPlaybackRate = 2.0;

/// The data of the last frame. The helper writes it; the parent only reads it.
struct SharedFrameHeader
{
	std::uint32_t width;  ///< The width of the texture, in pixels.
	std::uint32_t height; ///< The height of the texture, in pixels.
	std::uint32_t format; ///< `k_FrameFormatBgra8`.
	std::uint32_t reserved;
	/// The number of frames that the helper wrote into the texture. The helper adds 1 after `ReleaseSync`. The parent
	/// copies the texture only when the value changes.
	std::uint64_t frameCounter;
	/// The time of the last frame, in microseconds from the start of the helper.
	std::uint64_t frameTimeUs;
};

/// The start of the shared memory of one video.
struct SharedBlockHeader
{
	std::uint32_t magic;       ///< `k_SharedMagic`.
	std::uint32_t version;     ///< `k_ProtocolVersion`.
	std::uint32_t totalBytes;  ///< The size of the full block.
	std::uint32_t audioOffset; ///< The offset of the `AudioRingHeader` from the start of the block.
	SharedFrameHeader frame;
	std::array<std::byte, 16> reserved; ///< Zero. The audio ring starts on the next cache line.
};

static_assert(sizeof(SharedBlockHeader) == 64);
static_assert(sizeof(SharedBlockHeader) % alignof(AudioRingHeader) == 0);

/// The bytes of a shared block with an audio ring of this size.
[[nodiscard]] std::size_t SharedBlockBytes(std::uint32_t channels = k_AudioChannels,
                                           std::uint32_t capacityFrames = k_AudioCapacityFrames);

/// A view of one shared block. It does not own the memory.
class SharedBlock
{
public:
	/// Write a new block into the memory: the header, the size of the frames and an empty audio ring.
	[[nodiscard]] static ts4::Result<SharedBlock> Make(std::span<std::byte> memory, std::uint32_t width, std::uint32_t height,
	                                                   std::uint32_t capacityFrames = k_AudioCapacityFrames);

	/// Use a block that the helper wrote. The result is an error if the header or the version is not correct.
	[[nodiscard]] static ts4::Result<SharedBlock> Attach(std::span<std::byte> memory);

	/// The helper calls this after it writes a frame into the texture and releases the keyed mutex.
	void PublishFrame(std::uint64_t frameTimeUs);

	/// The number of frames that the helper wrote. The parent compares it with the last value that it saw.
	[[nodiscard]] std::uint64_t FrameCounter() const;
	[[nodiscard]] std::uint64_t FrameTimeUs() const;

	[[nodiscard]] const SharedFrameHeader& Frame() const { return _header->frame; }
	[[nodiscard]] AudioRing& Audio() { return _audio; }

private:
	SharedBlock(SharedBlockHeader* header, AudioRing audio)
	    : _header(header)
	    , _audio(audio)
	{
	}

	SharedBlockHeader* _header;
	AudioRing _audio;
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
