/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The tests of the reader of the bytes of a list of clips.

#include <cstdint>

#include <initializer_list>
#include <vector>

#include <core/PlaylistMessage.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

std::vector<std::byte> Bytes(std::initializer_list<std::uint8_t> values)
{
	std::vector<std::byte> out;
	for (const auto value : values)
	{
		out.push_back(static_cast<std::byte>(value));
	}
	return out;
}

// The key of the News clip of a TV in the fourth game test.
constexpr ScreenKey k_News {.instance = 0x5D630A58015C3E89, .type = 0x545AC67A, .group = 0x00BF458B};

TEST(PlaylistMessage, ReadsTheClipKeyOfAMessageOfTheGame)
{
	// The version 12, one clip key, then final_loop, mute_speed and speed_audio_clip_replacement.
	const auto message =
	    Bytes({0x08, 0x0C, 0x12, 0x15, 0x08, 0xFA, 0x8C, 0xEB, 0xA2, 0x05, 0x10, 0x8B, 0x8B, 0xFD, 0x05, 0x18,
	           0x89, 0xFD, 0xF0, 0x8A, 0x80, 0xCB, 0xC2, 0xB1, 0x5D, 0x18, 0x01, 0x28, 0x03, 0x38, 0xB4, 0x24});
	const auto keys = playlist::ReadClipKeys(message);
	ASSERT_TRUE(keys) << keys.GetError().message;
	ASSERT_EQ(keys->size(), 1U);
	EXPECT_EQ((*keys)[0], k_News);
}

TEST(PlaylistMessage, ReadsTheKeysInTheirOrder)
{
	const std::vector<ScreenKey> keys {k_News, {.instance = 0x0D432B7F5C14E35D, .type = 0x376840D7, .group = 0}};
	const auto read = playlist::ReadClipKeys(playlist::MakeMessage(3, keys));
	ASSERT_TRUE(read) << read.GetError().message;
	EXPECT_EQ(*read, keys);
}

TEST(PlaylistMessage, AnEmptyListHasNoKeys)
{
	const auto empty = playlist::ReadClipKeys({});
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->empty());
	const auto versionOnly = playlist::ReadClipKeys(playlist::MakeMessage(7, {}));
	ASSERT_TRUE(versionOnly);
	EXPECT_TRUE(versionOnly->empty());
}

TEST(PlaylistMessage, SkipsTheFieldsThatItDoesNotRead)
{
	// A fixed 32-bit field and a fixed 64-bit field before the key, and a text field in the key.
	auto message = Bytes({0x0D, 1, 2, 3, 4, 0x11, 1, 2, 3, 4, 5, 6, 7, 8});
	const auto key = Bytes({0x12, 0x09, 0x08, 0x07, 0x22, 0x02, 0x41, 0x42, 0x18, 0x81, 0x01});
	message.insert(message.end(), key.begin(), key.end());
	const auto keys = playlist::ReadClipKeys(message);
	ASSERT_TRUE(keys) << keys.GetError().message;
	ASSERT_EQ(keys->size(), 1U);
	EXPECT_EQ((*keys)[0], (ScreenKey {.instance = 0x81, .type = 7, .group = 0}));
}

TEST(PlaylistMessage, RefusesBytesThatAreNotCorrect)
{
	// A key that is longer than the message.
	EXPECT_FALSE(playlist::ReadClipKeys(Bytes({0x12, 0x20, 0x08, 0x01})));
	// A varint with no end.
	EXPECT_FALSE(playlist::ReadClipKeys(Bytes({0x08, 0x80, 0x80})));
	// The wire types 3 and 4 (groups) and the field number 0.
	EXPECT_FALSE(playlist::ReadClipKeys(Bytes({0x0B})));
	EXPECT_FALSE(playlist::ReadClipKeys(Bytes({0x00, 0x01})));
	// A key with a field that is not correct.
	EXPECT_FALSE(playlist::ReadClipKeys(Bytes({0x12, 0x01, 0x0F})));
}

TEST(PlaylistMessage, RefusesMoreThanTheLimits)
{
	const std::vector<ScreenKey> many(playlist::k_MaxClipKeys + 1, k_News);
	EXPECT_FALSE(playlist::ReadClipKeys(playlist::MakeMessage(1, many)));
	const std::vector<std::byte> large(playlist::k_MaxMessageBytes + 1, std::byte {0});
	EXPECT_FALSE(playlist::ReadClipKeys(large));
}

} // namespace
