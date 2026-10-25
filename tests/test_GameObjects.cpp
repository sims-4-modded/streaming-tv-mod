/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The tests of the reads of the video objects. The objects are fakes in a buffer, with the layout that the research
// read from the code of the game.

#include <cstdint>
#include <cstring>

#include <vector>

#include <core/GameObjects.h>
#include <core/PlaylistMessage.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

/// A fake memory of 64 KiB at a fixed address. The tests put objects into it.
class FakeMemory final: public MemoryAccess
{
public:
	static constexpr std::uintptr_t k_Base = 0x7F0000000000;

	FakeMemory()
	    : _bytes(0x10000, std::byte {0})
	{
	}

	[[nodiscard]] bool Read(std::uintptr_t address, std::span<std::byte> out) const override
	{
		if (!Holds(address, out.size()))
		{
			return false;
		}
		std::memcpy(out.data(), _bytes.data() + (address - k_Base), out.size());
		return true;
	}

	[[nodiscard]] std::span<std::uint8_t> Writable(std::uintptr_t address, std::size_t size) const override
	{
		if (!Holds(address, size) || address < _writableFrom)
		{
			return {};
		}
		return {reinterpret_cast<std::uint8_t*>(const_cast<std::byte*>(_bytes.data())) + (address - k_Base), size};
	}

	template <typename T>
	void Put(std::uintptr_t address, const T& value)
	{
		std::memcpy(_bytes.data() + (address - k_Base), &value, sizeof(T));
	}

	[[nodiscard]] std::uint8_t At(std::uintptr_t address) const
	{
		return std::to_integer<std::uint8_t>(_bytes[address - k_Base]);
	}

	/// Pages below this address cannot be written.
	void ProtectBelow(std::uintptr_t address) { _writableFrom = address; }

private:
	[[nodiscard]] bool Holds(std::uintptr_t address, std::size_t size) const
	{
		return address >= k_Base && size <= _bytes.size() && address - k_Base <= _bytes.size() - size;
	}

	std::vector<std::byte> _bytes;
	std::uintptr_t _writableFrom = 0;
};

constexpr game_layout::RendererLayout k_Layout {.framePlanes = {0x80, 0x88, 0x90},
                                                .frameFlip = 0xA1,
                                                .planeWidth = 0x0,
                                                .planeHeight = 0x4,
                                                .planePitch = 0x10,
                                                .planeData = 0x8,
                                                .rendererWidth = 0x8C,
                                                .rendererHeight = 0x90,
                                                .rendererChannel = 0xA0};
constexpr game_layout::ChannelLayout k_ChannelLayout {
    .messageObjectId = 0x30, .messageData = 0x48, .messageSize = 0x50, .channelClipKey = 0xA0};

constexpr std::uintptr_t k_Frame = FakeMemory::k_Base + 0x100;
constexpr std::uintptr_t k_Planes = FakeMemory::k_Base + 0x400;
constexpr std::uintptr_t k_Data = FakeMemory::k_Base + 0x1000;

/// Put a frame into the memory: the planes Y, U and V, with the pitch of Y and half of it for U and V.
void PutFrame(FakeMemory& memory, std::uint32_t width = 32, std::uint32_t height = 16, std::uint64_t pitch = 48)
{
	const std::array<std::uint32_t, 3> widths {width, (width + 1) / 2, (width + 1) / 2};
	const std::array<std::uint32_t, 3> heights {height, (height + 1) / 2, (height + 1) / 2};
	const std::array<std::uint64_t, 3> pitches {pitch, pitch / 2, pitch / 2};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto plane = k_Planes + index * 0x40;
		const auto data = k_Data + index * 0x1000;
		memory.Put(k_Frame + k_Layout.framePlanes[index], plane);
		memory.Put(plane + 0x0, widths[index]);
		memory.Put(plane + 0x4, heights[index]);
		memory.Put(plane + 0x8, data);
		memory.Put(plane + 0x10, pitches[index]);
	}
	memory.Put(k_Frame + k_Layout.frameFlip, std::uint8_t {1});
}

TEST(GameObjects, ReadsTheFrameAndWritesAnImageIntoIt)
{
	FakeMemory memory;
	PutFrame(memory);
	const auto frame = ReadGameFrame(memory, k_Frame, k_Layout);
	ASSERT_TRUE(frame);
	EXPECT_TRUE(frame->flip);
	EXPECT_EQ(frame->planes[0].width, 32U);
	EXPECT_EQ(frame->planes[1].height, 8U);
	EXPECT_EQ(frame->planes[2].pitch, 24U);

	std::vector<std::uint8_t> y(32 * 16, 200);
	std::vector<std::uint8_t> u(16 * 8, 60);
	std::vector<std::uint8_t> v(16 * 8, 70);
	const ConstI420Planes image {.y = y, .u = u, .v = v, .pitchY = 32, .pitchUV = 16, .width = 32, .height = 16};
	ASSERT_TRUE(WriteI420Frame(image, *frame, ScaleFilter::Bilinear, YuvRange::Limited));
	EXPECT_EQ(memory.At(k_Data), 200);
	EXPECT_EQ(memory.At(k_Data + 15 * 48 + 31), 200);
	// The padding of the row is not changed.
	EXPECT_EQ(memory.At(k_Data + 32), 0);
	EXPECT_EQ(memory.At(k_Data + 0x1000 + 7 * 24 + 15), 60);
	EXPECT_EQ(memory.At(k_Data + 0x2000), 70);
}

TEST(GameObjects, RefusesAFrameThatCannotBeWritten)
{
	FakeMemory memory;
	PutFrame(memory);
	memory.ProtectBelow(k_Data + 0x1800);
	EXPECT_FALSE(ReadGameFrame(memory, k_Frame, k_Layout));
}

TEST(GameObjects, RefusesAFrameWithBadSizes)
{
	FakeMemory memory;
	PutFrame(memory, 32, 16, 16);
	EXPECT_FALSE(ReadGameFrame(memory, k_Frame, k_Layout));
	PutFrame(memory, 0, 16, 48);
	EXPECT_FALSE(ReadGameFrame(memory, k_Frame, k_Layout));
	// A plane of 4096 rows does not fit into the memory.
	PutFrame(memory, 32, 4096, 48);
	EXPECT_FALSE(ReadGameFrame(memory, k_Frame, k_Layout));
}

TEST(GameObjects, RefusesANullFrameAndANullPlane)
{
	FakeMemory memory;
	EXPECT_FALSE(ReadGameFrame(memory, 0, k_Layout));
	EXPECT_FALSE(ReadGameFrame(memory, k_Frame, k_Layout));
	EXPECT_FALSE(ReadGameFrame(memory, 0x1000, k_Layout));
}

TEST(GameObjects, ReadsTheChannelAndTheSizeOfARenderer)
{
	FakeMemory memory;
	constexpr auto renderer = FakeMemory::k_Base + 0x2000;
	memory.Put(renderer + 0xA0, std::uintptr_t {0x123456789});
	memory.Put(renderer + 0x8C, std::uint32_t {640});
	memory.Put(renderer + 0x90, std::uint32_t {368});
	EXPECT_EQ(ReadRendererChannel(memory, renderer, k_Layout), 0x123456789U);
	EXPECT_EQ(ReadRendererChannel(memory, 0, k_Layout), 0U);
	EXPECT_EQ(ReadRendererSize(memory, renderer, k_Layout), (std::array<std::uint32_t, 2> {640, 368}));
}

TEST(GameObjects, ReadsTheClipKeyOfAChannel)
{
	FakeMemory memory;
	constexpr auto channel = FakeMemory::k_Base + 0x3000;
	EXPECT_FALSE(ReadChannelClipKey(memory, channel, k_ChannelLayout));
	memory.Put(channel + 0xA0, std::uint64_t {0x8A50833624769271});
	memory.Put(channel + 0xA8, std::uint32_t {0x376840D7});
	memory.Put(channel + 0xAC, std::uint32_t {0});
	const auto key = ReadChannelClipKey(memory, channel, k_ChannelLayout);
	ASSERT_TRUE(key);
	EXPECT_EQ(*key, (ScreenKey {.instance = 0x8A50833624769271, .type = 0x376840D7, .group = 0}));
}

TEST(GameObjects, ReadsTheMessageAndTheListener)
{
	FakeMemory memory;
	constexpr auto message = FakeMemory::k_Base + 0x4100;
	constexpr auto bytes = FakeMemory::k_Base + 0x4400;
	constexpr auto state = FakeMemory::k_Base + 0x4200;
	memory.Put(message + 0x30, std::uint64_t {0x1234});
	const ScreenKey key {.instance = 0x5D630A58015C3E89, .type = 0x545AC67A, .group = 0x00BF458B};
	const auto encoded = playlist::MakeMessage(12, std::span(&key, 1));
	for (std::size_t index = 0; index < encoded.size(); ++index)
	{
		memory.Put(bytes + index, encoded[index]);
	}
	memory.Put(message + 0x48, bytes);
	memory.Put(message + 0x50, static_cast<std::int32_t>(encoded.size()));
	std::array<float, 16> matrix {};
	matrix[0] = 1.0F;
	matrix[15] = 1.0F;
	memory.Put(state + 0x40, matrix);
	EXPECT_EQ(ReadMessageObjectId(memory, message, k_ChannelLayout), 0x1234U);
	const auto keys = ReadMessageClipKeys(memory, message, k_ChannelLayout);
	ASSERT_TRUE(keys) << keys.GetError().message;
	EXPECT_EQ(*keys, std::vector<ScreenKey> {key});
	EXPECT_EQ(ReadListenerMatrix(memory, state, 0x40), matrix);
	EXPECT_FALSE(ReadListenerMatrix(memory, FakeMemory::k_Base + 0xFFF8, 0x40));
}

TEST(GameObjects, RefusesTheBytesOfAMessageThatTheModCannotRead)
{
	FakeMemory memory;
	constexpr auto message = FakeMemory::k_Base + 0x4100;
	// A size that is not correct, then bytes outside the memory.
	memory.Put(message + 0x48, FakeMemory::k_Base + 0x4400);
	memory.Put(message + 0x50, std::int32_t {-1});
	EXPECT_FALSE(ReadMessageClipKeys(memory, message, k_ChannelLayout));
	memory.Put(message + 0x48, std::uintptr_t {0x1000});
	memory.Put(message + 0x50, std::int32_t {8});
	EXPECT_FALSE(ReadMessageClipKeys(memory, message, k_ChannelLayout));
	EXPECT_FALSE(ReadMessageClipKeys(memory, 0, k_ChannelLayout));
	// No bytes: an empty list.
	memory.Put(message + 0x50, std::int32_t {0});
	const auto empty = ReadMessageClipKeys(memory, message, k_ChannelLayout);
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->empty());
}

} // namespace
