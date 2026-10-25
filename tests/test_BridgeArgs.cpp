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
