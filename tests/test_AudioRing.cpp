/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <array>
#include <thread>
#include <vector>

#include <core/AudioRing.h>
#include <gtest/gtest.h>

using ts4_mod::youtube_tv::AudioRing;
using ts4_mod::youtube_tv::AudioRingHeader;

namespace
{

/// Memory for a ring, aligned as the header needs.
class RingMemory
{
public:
	explicit RingMemory(std::size_t bytes)
	    : _blocks((bytes + sizeof(Block) - 1) / sizeof(Block))
	{
	}

	[[nodiscard]] std::span<std::byte> Bytes()
	{
		return {reinterpret_cast<std::byte*>(_blocks.data()), _blocks.size() * sizeof(Block)};
	}

private:
	struct alignas(64) Block
	{
		std::array<std::byte, 64> bytes;
	};

	std::vector<Block> _blocks;
};

/// Interleaved stereo frames with the values 0, 1, 2 for the left channel and the negative values for the right.
[[nodiscard]] std::vector<float> Frames(int first, int count)
{
	std::vector<float> samples;
	for (int frame = first; frame < first + count; ++frame)
	{
		samples.push_back(static_cast<float>(frame));
		samples.push_back(-static_cast<float>(frame));
	}
	return samples;
}

} // namespace

TEST(AudioRing, ReadsTheFramesThatItWrote)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring) << ring.GetError().message;
	EXPECT_EQ(ring->AvailableFrames(), 0U);
	EXPECT_EQ(ring->FreeFrames(), 8U);

	EXPECT_EQ(ring->Write(Frames(0, 5)), 5U);
	EXPECT_EQ(ring->AvailableFrames(), 5U);

	std::vector<float> out(10);
	EXPECT_EQ(ring->Read(out), 5U);
	EXPECT_EQ(out, Frames(0, 5));
	EXPECT_EQ(ring->AvailableFrames(), 0U);
	EXPECT_EQ(ring->DroppedFrames(), 0U);
	EXPECT_EQ(ring->MissingFrames(), 0U);
}

TEST(AudioRing, WrapsAtTheEnd)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring);
	std::vector<float> out(12);
	for (int round = 0; round < 10; ++round)
	{
		ASSERT_EQ(ring->Write(Frames(round * 6, 6)), 6U) << round;
		ASSERT_EQ(ring->Read(out), 6U) << round;
		ASSERT_EQ(out, Frames(round * 6, 6)) << round;
	}
}

TEST(AudioRing, DropsTheNewFramesOnOverrun)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring);
	EXPECT_EQ(ring->Write(Frames(0, 6)), 6U);
	EXPECT_EQ(ring->Write(Frames(6, 6)), 2U);
	EXPECT_EQ(ring->DroppedFrames(), 4U);
	EXPECT_EQ(ring->FreeFrames(), 0U);
	EXPECT_EQ(ring->Write(Frames(12, 1)), 0U);
	EXPECT_EQ(ring->DroppedFrames(), 5U);

	std::vector<float> out(16);
	EXPECT_EQ(ring->Read(out), 8U);
	EXPECT_EQ(std::vector<float>(out.begin(), out.begin() + 16), Frames(0, 8));
}

TEST(AudioRing, ReadsWhatItHasOnUnderrun)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring);
	EXPECT_EQ(ring->Write(Frames(0, 3)), 3U);
	std::vector<float> out(8, 99.0F);
	EXPECT_EQ(ring->Read(out), 3U);
	EXPECT_EQ(ring->MissingFrames(), 1U);
	EXPECT_EQ(std::vector<float>(out.begin(), out.begin() + 6), Frames(0, 3));
	EXPECT_EQ(out[6], 99.0F);

	EXPECT_EQ(ring->Read(out), 0U);
	EXPECT_EQ(ring->MissingFrames(), 5U);
}

TEST(AudioRing, DropsTheOldestFramesToKeepTheDelaySmall)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring);
	ring->Write(Frames(0, 7));
	EXPECT_EQ(ring->DropOldest(2), 5U);
	EXPECT_EQ(ring->DropOldest(2), 0U);
	std::vector<float> out(4);
	EXPECT_EQ(ring->Read(out), 2U);
	EXPECT_EQ(out, Frames(5, 2));
}

TEST(AudioRing, InterleavesPlanarFrames)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 8);
	ASSERT_TRUE(ring);
	const std::array left {1.0F, 2.0F, 3.0F};
	const std::array right {-1.0F, -2.0F, -3.0F};
	const std::array<const float*, 2> planes {left.data(), right.data()};
	EXPECT_EQ(ring->WritePlanar(planes, 3), 3U);
	std::vector<float> out(6);
	EXPECT_EQ(ring->Read(out), 3U);
	EXPECT_EQ(out, Frames(1, 3));

	// A mono stream gives the same sample to the two channels.
	const std::array<const float*, 1> mono {left.data()};
	EXPECT_EQ(ring->WritePlanar(mono, 3), 3U);
	EXPECT_EQ(ring->Read(out), 3U);
	EXPECT_EQ(out, (std::vector<float> {1.0F, 1.0F, 2.0F, 2.0F, 3.0F, 3.0F}));
}

TEST(AudioRing, AttachesToARingThatItMade)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 16));
	auto producer = AudioRing::Make(memory.Bytes(), 44100, 2, 16);
	ASSERT_TRUE(producer);
	producer->Write(Frames(0, 4));
	producer->StartStream(48000);

	auto consumer = AudioRing::Attach(memory.Bytes());
	ASSERT_TRUE(consumer) << consumer.GetError().message;
	EXPECT_EQ(consumer->SampleRate(), 48000U);
	EXPECT_EQ(consumer->StreamCounter(), 1U);
	EXPECT_EQ(consumer->Channels(), 2U);
	EXPECT_EQ(consumer->CapacityFrames(), 16U);
	EXPECT_EQ(consumer->AvailableFrames(), 4U);
}

TEST(AudioRing, RefusesMemoryThatIsNotCorrect)
{
	RingMemory memory(AudioRing::RequiredBytes(2, 8));
	EXPECT_FALSE(AudioRing::Make(memory.Bytes().first(AudioRing::RequiredBytes(2, 8) - 1), 48000, 2, 8));
	EXPECT_FALSE(AudioRing::Make(memory.Bytes(), 48000, 0, 8));
	EXPECT_FALSE(AudioRing::Make(memory.Bytes(), 48000, 9, 8));
	EXPECT_FALSE(AudioRing::Make(memory.Bytes(), 48000, 2, 0));
	EXPECT_FALSE(AudioRing::Make(memory.Bytes().subspan(4), 48000, 2, 4));

	RingMemory empty(sizeof(AudioRingHeader));
	EXPECT_FALSE(AudioRing::Attach(empty.Bytes()));
}

TEST(AudioRing, KeepsTheOrderWithTwoThreads)
{
	constexpr int k_TotalFrames = 200000;
	RingMemory memory(AudioRing::RequiredBytes(2, 256));
	auto ring = AudioRing::Make(memory.Bytes(), 48000, 2, 256);
	ASSERT_TRUE(ring);

	std::thread producer([&] {
		int next = 0;
		while (next < k_TotalFrames)
		{
			const int count = std::min(37, k_TotalFrames - next);
			if (ring->FreeFrames() >= static_cast<std::size_t>(count))
			{
				ring->Write(Frames(next, count));
				next += count;
			}
			else
			{
				std::this_thread::yield();
			}
		}
	});

	int expected = 0;
	bool inOrder = true;
	std::vector<float> out(2 * 50);
	while (expected < k_TotalFrames)
	{
		if (ring->AvailableFrames() < 50 && k_TotalFrames - expected >= 50)
		{
			std::this_thread::yield();
			continue;
		}
		const auto count = ring->Read(out);
		for (std::size_t frame = 0; frame < count; ++frame)
		{
			inOrder = inOrder && out[frame * 2] == static_cast<float>(expected) &&
			          out[frame * 2 + 1] == -static_cast<float>(expected);
			++expected;
		}
	}
	producer.join();
	EXPECT_TRUE(inOrder);
	EXPECT_EQ(ring->DroppedFrames(), 0U);
}
