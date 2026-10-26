/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cmath>

#include <array>
#include <string>

#include <core/BridgeArgs.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;
using namespace ts4_mod::youtube_tv::bridge;

/// The value of a result, or `fallback` for an error.
[[nodiscard]] std::uint64_t Or(const ts4::Result<std::uint64_t>& result, std::uint64_t fallback)
{
	return result ? *result : fallback;
}

TEST(BridgeArgs, ReadsAUrl)
{
	const auto url = ReadUrl("https://youtu.be/M7lc1UVf-VE");
	ASSERT_TRUE(url);
	EXPECT_EQ(*url, "https://youtu.be/M7lc1UVf-VE");
	EXPECT_FALSE(ReadUrl(nullptr));
	EXPECT_FALSE(ReadUrl(""));
	EXPECT_FALSE(ReadUrl("https://youtu.be/\nM7lc1UVf-VE"));
}

TEST(BridgeArgs, RefusesAUrlThatIsTooLong)
{
	const std::string longest(k_MaxUrlBytes, 'a');
	EXPECT_TRUE(ReadUrl(longest.c_str()));
	const std::string tooLong(k_MaxUrlBytes + 1, 'a');
	EXPECT_FALSE(ReadUrl(tooLong.c_str()));
}

TEST(BridgeArgs, ReadsAPosition)
{
	const std::array<float, 3> position {1.0F, -2.0F, 3.5F};
	const auto read = ReadPosition(position.data());
	ASSERT_TRUE(read);
	EXPECT_EQ(*read, position);
	EXPECT_FALSE(ReadPosition(nullptr));
	const std::array<float, 3> nan {NAN, 0.0F, 0.0F};
	EXPECT_FALSE(ReadPosition(nan.data()));
	const std::array<float, 3> far {0.0F, 2.0e5F, 0.0F};
	EXPECT_FALSE(ReadPosition(far.data()));
}

TEST(BridgeArgs, MakesTheOrientationAUnitQuaternion)
{
	const std::array<float, 4> quaternion {0.0F, 2.0F, 0.0F, 0.0F};
	const auto read = ReadOrientation(quaternion.data());
	ASSERT_TRUE(read);
	EXPECT_FLOAT_EQ((*read)[1], 1.0F);
	EXPECT_FALSE(ReadOrientation(nullptr));
	const std::array<float, 4> zero {};
	EXPECT_FALSE(ReadOrientation(zero.data()));
	const std::array<float, 4> infinite {INFINITY, 0.0F, 0.0F, 1.0F};
	EXPECT_FALSE(ReadOrientation(infinite.data()));
}

TEST(BridgeArgs, ATransformNeedsBothPointers)
{
	const std::array<float, 3> position {1.0F, 2.0F, 3.0F};
	const std::array<float, 4> orientation {0.0F, 0.0F, 0.0F, 1.0F};
	const auto transform = ReadTransform(position.data(), orientation.data());
	ASSERT_TRUE(transform);
	EXPECT_EQ(transform->position, position);
	EXPECT_FALSE(ReadTransform(position.data(), nullptr));
	EXPECT_FALSE(ReadTransform(nullptr, orientation.data()));
}

TEST(BridgeArgs, ReadsAScreenKey)
{
	const auto key = ReadScreenKey(0x8A50833624769271, 0x376840D7, 0);
	ASSERT_TRUE(key);
	EXPECT_EQ(key->instance, 0x8A50833624769271U);
	EXPECT_FALSE(ReadScreenKey(1, 0, 0));
}

TEST(BridgeArgs, ReadsTheClipKeysOfAPlaylist)
{
	// Two rows: a little-endian u64 instance, u32 type and u32 group.
	const std::array<std::uint8_t, 32> bytes {
	    0xC3, 0x1E, 0x0F, 0x8E, 0x03, 0xC4, 0x6F, 0xEA, 0xD7, 0x40, 0x68, 0x37, 0x00, 0x00, 0x00, 0x00, //
	    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x80, //
	};
	const auto keys = ReadClipKeys(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
	ASSERT_TRUE(keys);
	ASSERT_EQ(keys->size(), 2U);
	EXPECT_EQ((*keys)[0], (ScreenKey {.instance = 0xEA6FC4038E0F1EC3, .type = 0x376840D7, .group = 0}));
	EXPECT_EQ((*keys)[1], (ScreenKey {.instance = 1, .type = 2, .group = 0x80000003}));
	// No keys.
	const auto none = ReadClipKeys(nullptr, 0);
	ASSERT_TRUE(none);
	EXPECT_TRUE(none->empty());
}

TEST(BridgeArgs, RefusesBadClipKeys)
{
	const std::array<std::uint8_t, 32> bytes {};
	EXPECT_FALSE(ReadClipKeys(bytes.data(), 15));
	EXPECT_FALSE(ReadClipKeys(bytes.data(), 17));
	EXPECT_FALSE(ReadClipKeys(nullptr, 16));
	// More keys than the limit. The function checks the size before it reads a byte.
	const auto tooMany = static_cast<std::uint32_t>((SessionManager::k_MaxPlaylistClips + 1) * k_ClipKeyBytes);
	EXPECT_FALSE(ReadClipKeys(bytes.data(), tooMany));
	EXPECT_FALSE(ReadClipKeys(bytes.data(), UINT32_MAX - 15));
}

TEST(BridgeArgs, ReadsTheSpeed)
{
	const auto speed = ReadSpeed(5, 1.0, 1);
	ASSERT_TRUE(speed);
	EXPECT_EQ(speed->mode, ClockSpeedMode::SuperSpeed3);
	EXPECT_TRUE(speed->superSpeed);
	EXPECT_FALSE(ReadSpeed(6, 1.0, 0));
	EXPECT_FALSE(ReadSpeed(1, -1.0, 0));
	EXPECT_FALSE(ReadSpeed(1, NAN, 0));
	EXPECT_FALSE(ReadSpeed(1, 1.0, 2));
}

TEST(BridgeArgs, ReadsTheObjectIdOfAQuery)
{
	EXPECT_EQ(Or(ReadObjectIdField(nlohmann::json {{"objectId", 1234}}, std::nullopt), 0), 1234U);
	EXPECT_EQ(Or(ReadObjectIdField(nlohmann::json {{"objectId", "0x1F"}}, std::nullopt), 0), 0x1FU);
	EXPECT_EQ(Or(ReadObjectIdField(nlohmann::json {{"objectId", "77"}}, std::nullopt), 0), 77U);
	EXPECT_EQ(Or(ReadObjectIdField(nlohmann::json {{"objectId", "any"}}, std::nullopt), 1), k_AnyObject);
	EXPECT_EQ(Or(ReadObjectIdField(nlohmann::json::object(), k_AnyObject), 1), k_AnyObject);
	EXPECT_FALSE(ReadObjectIdField(nlohmann::json::object(), std::nullopt));
	EXPECT_FALSE(ReadObjectIdField(nlohmann::json {{"objectId", -1}}, std::nullopt));
	EXPECT_FALSE(ReadObjectIdField(nlohmann::json {{"objectId", "tv"}}, std::nullopt));
	EXPECT_FALSE(ReadObjectIdField(nlohmann::json {{"objectId", "0x"}}, std::nullopt));
	EXPECT_FALSE(ReadObjectIdField(nlohmann::json::array(), std::nullopt));
}

TEST(BridgeArgs, ReadsTheTransformOfAQuery)
{
	const auto transform = ReadTransformFields(nlohmann::json {{"position", {1, 2, 3}}});
	ASSERT_TRUE(transform) << transform.GetError().message;
	EXPECT_EQ(transform->position, (std::array {1.0F, 2.0F, 3.0F}));
	EXPECT_EQ(transform->orientation, (std::array {0.0F, 0.0F, 0.0F, 1.0F}));
	EXPECT_FALSE(ReadTransformFields(nlohmann::json {{"position", {1, 2}}}));
	EXPECT_FALSE(ReadTransformFields(nlohmann::json {{"position", {1, 2, 3}}, {"orientation", {0, 0, 0, 0}}}));
	EXPECT_FALSE(ReadTransformFields(nlohmann::json {{"position", {1, "a", 3}}}));
}

TEST(BridgeArgs, CopiesTheTextWithAZero)
{
	std::array<char, 16> buffer {};
	buffer.fill('x');
	EXPECT_EQ(CopyText("the error", buffer.data(), static_cast<std::uint32_t>(buffer.size())), 9U);
	EXPECT_STREQ(buffer.data(), "the error");
	EXPECT_EQ(CopyText("the error", nullptr, 16), 9U);
	EXPECT_EQ(CopyText("the error", buffer.data(), 0), 9U);
	EXPECT_EQ(CopyText("", buffer.data(), 4), 0U);
	EXPECT_STREQ(buffer.data(), "");
}

TEST(BridgeArgs, CutsALongTextAtACharacter)
{
	std::array<char, 5> buffer {};
	EXPECT_EQ(CopyText("abcdefgh", buffer.data(), 5), 8U);
	EXPECT_STREQ(buffer.data(), "abcd");
	// "abc" and a character of two bytes: four bytes do not hold the full character.
	EXPECT_EQ(CopyText("abc\xC3\xA9", buffer.data(), 5), 5U);
	EXPECT_STREQ(buffer.data(), "abc");
	EXPECT_EQ(CopyText("abc\xC3\xA9", buffer.data(), 4), 5U);
	EXPECT_STREQ(buffer.data(), "abc");
}

} // namespace
