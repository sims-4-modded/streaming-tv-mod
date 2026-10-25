/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "AudioRing.h"

#include <cstring>

#include <algorithm>
#include <memory>
#include <new>

namespace ts4_mod::youtube_tv
{
namespace
{

using Frames = std::uint64_t;

[[nodiscard]] Frames LoadAcquire(Frames& value)
{
	return std::atomic_ref(value).load(std::memory_order_acquire);
}

[[nodiscard]] Frames LoadRelaxed(Frames& value)
{
	return std::atomic_ref(value).load(std::memory_order_relaxed);
}

void StoreRelease(Frames& value, Frames newValue)
{
	std::atomic_ref(value).store(newValue, std::memory_order_release);
}

void AddRelaxed(Frames& value, Frames amount)
{
	std::atomic_ref(value).fetch_add(amount, std::memory_order_relaxed);
}

} // namespace

std::size_t AudioRing::RequiredBytes(std::uint32_t channels, std::uint32_t capacityFrames)
{
	return sizeof(AudioRingHeader) + std::size_t {channels} * capacityFrames * sizeof(float);
}

ts4::Result<AudioRing> AudioRing::Make(std::span<std::byte> memory, std::uint32_t sampleRate, std::uint32_t channels,
                                       std::uint32_t capacityFrames)
{
	if (channels == 0 || channels > k_MaxAudioChannels)
	{
		return ts4::MakeError("the ring cannot have {} channels", channels);
	}
	if (capacityFrames == 0 || sampleRate == 0)
	{
		return ts4::MakeError("the capacity and the sample rate of the ring must be more than 0");
	}
	if (reinterpret_cast<std::uintptr_t>(memory.data()) % alignof(AudioRingHeader) != 0)
	{
		return ts4::MakeError("the memory of the ring is not aligned to {} bytes", alignof(AudioRingHeader));
	}
	if (memory.size() < RequiredBytes(channels, capacityFrames))
	{
		return ts4::MakeError("the ring needs {} bytes, but the memory has {}", RequiredBytes(channels, capacityFrames),
		                      memory.size());
	}
	std::memset(memory.data(), 0, RequiredBytes(channels, capacityFrames));
	auto* header = new (memory.data()) AudioRingHeader {};
	header->sampleRate = sampleRate;
	header->channels = channels;
	header->capacityFrames = capacityFrames;
	std::atomic_thread_fence(std::memory_order_release);
	return AudioRing(header, reinterpret_cast<float*>(memory.data() + sizeof(AudioRingHeader)));
}

ts4::Result<AudioRing> AudioRing::Attach(std::span<std::byte> memory)
{
	if (memory.size() < sizeof(AudioRingHeader))
	{
		return ts4::MakeError("the memory is too small for the header of a ring");
	}
	if (reinterpret_cast<std::uintptr_t>(memory.data()) % alignof(AudioRingHeader) != 0)
	{
		return ts4::MakeError("the memory of the ring is not aligned to {} bytes", alignof(AudioRingHeader));
	}
	auto* header = std::launder(reinterpret_cast<AudioRingHeader*>(memory.data()));
	if (header->channels == 0 || header->channels > k_MaxAudioChannels || header->capacityFrames == 0)
	{
		return ts4::MakeError("the header of the ring is not correct");
	}
	if (memory.size() < RequiredBytes(header->channels, header->capacityFrames))
	{
		return ts4::MakeError("the ring is larger than its memory");
	}
	return AudioRing(header, reinterpret_cast<float*>(memory.data() + sizeof(AudioRingHeader)));
}

std::size_t AudioRing::SampleIndex(std::uint64_t frame) const
{
	return static_cast<std::size_t>(frame % _header->capacityFrames) * _header->channels;
}

std::size_t AudioRing::AvailableFrames() const
{
	const auto write = LoadAcquire(_header->writeFrame);
	const auto read = LoadAcquire(_header->readFrame);
	return static_cast<std::size_t>(write - read);
}

std::size_t AudioRing::FreeFrames() const
{
	return _header->capacityFrames - AvailableFrames();
}

std::size_t AudioRing::Write(std::span<const float> interleaved)
{
	const std::size_t channels = _header->channels;
	const std::size_t frames = interleaved.size() / channels;
	const auto write = LoadRelaxed(_header->writeFrame);
	const auto read = LoadAcquire(_header->readFrame);
	const auto free = _header->capacityFrames - static_cast<std::size_t>(write - read);
	const auto count = std::min(frames, free);

	// Copy in a maximum of two parts: to the end of the ring, and from the start of the ring.
	const auto first = std::min<std::size_t>(count, _header->capacityFrames - (write % _header->capacityFrames));
	std::memcpy(_samples + SampleIndex(write), interleaved.data(), first * channels * sizeof(float));
	std::memcpy(_samples, interleaved.data() + first * channels, (count - first) * channels * sizeof(float));

	StoreRelease(_header->writeFrame, write + count);
	if (count < frames)
	{
		AddRelaxed(_header->droppedFrames, frames - count);
	}
	return count;
}

std::size_t AudioRing::WritePlanar(std::span<const float* const> planes, std::size_t frames)
{
	if (planes.empty() || planes.front() == nullptr)
	{
		return 0;
	}
	const std::size_t channels = _header->channels;
	const auto write = LoadRelaxed(_header->writeFrame);
	const auto read = LoadAcquire(_header->readFrame);
	const auto free = _header->capacityFrames - static_cast<std::size_t>(write - read);
	const auto count = std::min(frames, free);

	for (std::size_t frame = 0; frame < count; ++frame)
	{
		float* out = _samples + SampleIndex(write + frame);
		for (std::size_t channel = 0; channel < channels; ++channel)
		{
			const float* plane = channel < planes.size() && planes[channel] != nullptr ? planes[channel] : planes[0];
			out[channel] = plane[frame];
		}
	}

	StoreRelease(_header->writeFrame, write + count);
	if (count < frames)
	{
		AddRelaxed(_header->droppedFrames, frames - count);
	}
	return count;
}

void AudioRing::StartStream(std::uint32_t sampleRate)
{
	std::atomic_ref(_header->sampleRate).store(sampleRate, std::memory_order_relaxed);
	std::atomic_ref(_header->streamCounter).fetch_add(1, std::memory_order_release);
}

std::size_t AudioRing::Read(std::span<float> out)
{
	const std::size_t channels = _header->channels;
	const std::size_t wanted = out.size() / channels;
	const auto read = LoadRelaxed(_header->readFrame);
	const auto write = LoadAcquire(_header->writeFrame);
	const auto available = static_cast<std::size_t>(write - read);
	const auto count = std::min(wanted, available);

	const auto first = std::min<std::size_t>(count, _header->capacityFrames - (read % _header->capacityFrames));
	std::memcpy(out.data(), _samples + SampleIndex(read), first * channels * sizeof(float));
	std::memcpy(out.data() + first * channels, _samples, (count - first) * channels * sizeof(float));

	StoreRelease(_header->readFrame, read + count);
	if (count < wanted)
	{
		AddRelaxed(_header->missingFrames, wanted - count);
	}
	return count;
}

std::size_t AudioRing::DropOldest(std::size_t keepFrames)
{
	const auto read = LoadRelaxed(_header->readFrame);
	const auto write = LoadAcquire(_header->writeFrame);
	const auto available = static_cast<std::size_t>(write - read);
	if (available <= keepFrames)
	{
		return 0;
	}
	const auto drop = available - keepFrames;
	StoreRelease(_header->readFrame, read + drop);
	return drop;
}

std::uint32_t AudioRing::SampleRate() const
{
	return std::atomic_ref(_header->sampleRate).load(std::memory_order_relaxed);
}

std::uint32_t AudioRing::StreamCounter() const
{
	return std::atomic_ref(_header->streamCounter).load(std::memory_order_acquire);
}

std::uint64_t AudioRing::DroppedFrames() const
{
	return LoadRelaxed(_header->droppedFrames);
}

std::uint64_t AudioRing::MissingFrames() const
{
	return LoadRelaxed(_header->missingFrames);
}

} // namespace ts4_mod::youtube_tv
