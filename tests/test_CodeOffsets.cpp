/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The tests of the reader of field offsets. The byte sequences are the instructions that the research of the mod
// found in the code of the game. Each test keeps only these instructions, with filler bytes between them.

#include <cstdint>

#include <initializer_list>
#include <vector>

#include <core/CodeOffsets.h>
#include <core/VideoLayout.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

/// Make a fake window of code: the instruction groups in order, with filler bytes between them.
std::vector<std::byte> Code(std::initializer_list<std::initializer_list<std::uint8_t>> groups)
{
	std::vector<std::byte> code(16, std::byte {0xCC});
	for (const auto& group : groups)
	{
		for (const auto value : group)
		{
			code.push_back(static_cast<std::byte>(value));
		}
		code.insert(code.end(), 16, std::byte {0xCC});
	}
	return code;
}

// The instructions of the function that copies the planes.
const std::initializer_list<std::uint8_t> k_Planes = {0x4D, 0x8B, 0xAF, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24, 0x50,
                                                      0x49, 0x8B, 0x87, 0x88, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x58,
                                                      0x49, 0x8B, 0x87, 0x90, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x60};
const std::initializer_list<std::uint8_t> k_PlaneFields = {0x8B, 0x43, 0x04, 0x89, 0x44, 0x24, 0x28, 0x8B, 0x03,
                                                           0x89, 0x44, 0x24, 0x20, 0x4C, 0x8B, 0x4B, 0x10, 0x4C,
                                                           0x8B, 0x43, 0x08, 0x33, 0xD2, 0x48, 0x8B, 0x4B, 0x18};
const std::initializer_list<std::uint8_t> k_RendererSize = {0x44, 0x8B, 0x86, 0x90, 0x00, 0x00, 0x00, 0x8B,
                                                            0x96, 0x8C, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4E};
const std::initializer_list<std::uint8_t> k_RendererChannel = {0x48, 0x8B, 0x8E, 0xA0, 0x00, 0x00, 0x00,
                                                               0x48, 0x85, 0xC9, 0x74, 0x05, 0xE8};
const std::initializer_list<std::uint8_t> k_FrameFlip = {0x41, 0x0F, 0xB6, 0x87, 0xA1, 0x00,
                                                         0x00, 0x00, 0x84, 0xC0, 0x74, 0x26};
// The other functions.
const std::initializer_list<std::uint8_t> k_ComponentChannel = {0x48, 0x8B, 0xD9, 0x41, 0x8B, 0xF9,
                                                                0x48, 0x8B, 0x49, 0x20, 0x49, 0x8B};
const std::initializer_list<std::uint8_t> k_MessageObjectId = {0x48, 0x8B, 0xD9, 0x48, 0x83, 0xC1, 0x28, 0x48, 0x8D, 0x55};
const std::initializer_list<std::uint8_t> k_ChannelClipKey = {0x48, 0x8D, 0x93, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8};
const std::initializer_list<std::uint8_t> k_ListenerMatrix = {0x41, 0x0F, 0x10, 0x8E, 0xF0, 0x00,
                                                              0x00, 0x00, 0x0F, 0x5C, 0x4E, 0x40};

TEST(CodeOffsets, ReadsADwordDisplacement)
{
	const auto code = Code({k_RendererChannel});
	const auto value = ReadDisplacement(code, game_layout::k_RendererChannel);
	ASSERT_TRUE(value) << value.GetError().message;
	EXPECT_EQ(*value, 0xA0);
}

TEST(CodeOffsets, ReadsAByteDisplacementWithItsSign)
{
	const DisplacementRule rule {
	    .name = "test", .pattern = "48 8B 49 ??", .at = 3, .size = DisplacementSize::Byte, .min = -0x80, .max = 0x7F};
	const auto code = Code({{0x48, 0x8B, 0x49, 0xF8}});
	const auto value = ReadDisplacement(code, rule);
	ASSERT_TRUE(value) << value.GetError().message;
	EXPECT_EQ(*value, -8);
	EXPECT_FALSE(ReadFieldOffset(code, rule));
}

TEST(CodeOffsets, ReadsTheFormWithNoDisplacementAsZero)
{
	const auto code = Code({k_PlaneFields});
	const auto value = ReadDisplacement(code, game_layout::k_PlaneWidth);
	ASSERT_TRUE(value) << value.GetError().message;
	EXPECT_EQ(*value, 0);
}

TEST(CodeOffsets, RefusesAPatternWithNoMatch)
{
	const auto code = Code({k_FrameFlip});
	EXPECT_FALSE(ReadDisplacement(code, game_layout::k_RendererChannel));
}

TEST(CodeOffsets, RefusesAPatternWithTwoMatches)
{
	const auto code = Code({k_RendererChannel, k_RendererChannel});
	const auto value = ReadDisplacement(code, game_layout::k_RendererChannel);
	ASSERT_FALSE(value);
}

TEST(CodeOffsets, RefusesAValueOutOfTheRange)
{
	const auto code = Code({{0x48, 0x8B, 0x8E, 0x00, 0x10, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x05, 0xE8}});
	EXPECT_FALSE(ReadDisplacement(code, game_layout::k_RendererChannel));
}

TEST(CodeOffsets, RefusesAValueThatIsNotAligned)
{
	const auto code = Code({{0x48, 0x8B, 0x8E, 0xA4, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x05, 0xE8}});
	EXPECT_FALSE(ReadDisplacement(code, game_layout::k_RendererChannel));
}

TEST(CodeOffsets, RefusesADisplacementOutsideThePattern)
{
	const DisplacementRule rule {
	    .name = "test", .pattern = "48 8B 49 ??", .at = 2, .size = DisplacementSize::Dword, .min = 0, .max = 0x100};
	const auto code = Code({{0x48, 0x8B, 0x49, 0x20, 0x00, 0x00, 0x00}});
	EXPECT_FALSE(ReadDisplacement(code, rule));
}

TEST(CodeOffsets, RefusesABadPattern)
{
	const DisplacementRule rule {.name = "test", .pattern = "48 8G", .at = 0, .size = DisplacementSize::None};
	const auto code = Code({{0x48}});
	EXPECT_FALSE(ReadDisplacement(code, rule));
}

TEST(VideoLayout, ReadsTheRendererLayoutFromTheInstructionsOfTheGame)
{
	const auto code = Code({k_RendererSize, k_RendererChannel, k_Planes, k_PlaneFields, k_FrameFlip});
	const auto layout = game_layout::ReadRendererLayout(code);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->framePlanes[0], 0x80U);
	EXPECT_EQ(layout->framePlanes[1], 0x88U);
	EXPECT_EQ(layout->framePlanes[2], 0x90U);
	EXPECT_EQ(layout->frameFlip, 0xA1U);
	EXPECT_EQ(layout->planeWidth, 0x0U);
	EXPECT_EQ(layout->planeHeight, 0x4U);
	EXPECT_EQ(layout->planeData, 0x8U);
	EXPECT_EQ(layout->planePitch, 0x10U);
	EXPECT_EQ(layout->rendererWidth, 0x8CU);
	EXPECT_EQ(layout->rendererHeight, 0x90U);
	EXPECT_EQ(layout->rendererChannel, 0xA0U);
}

TEST(VideoLayout, RefusesFieldsThatOverlap)
{
	// The U plane at the same offset as the Y plane.
	const std::initializer_list<std::uint8_t> planes = {0x4D, 0x8B, 0xAF, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24, 0x50,
	                                                    0x49, 0x8B, 0x87, 0x80, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x58,
	                                                    0x49, 0x8B, 0x87, 0x90, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x60};
	const auto code = Code({k_RendererSize, k_RendererChannel, planes, k_PlaneFields, k_FrameFlip});
	EXPECT_FALSE(game_layout::ReadRendererLayout(code));
}

TEST(VideoLayout, RefusesARendererLayoutWithAMissingRule)
{
	const auto code = Code({k_RendererSize, k_Planes, k_PlaneFields, k_FrameFlip});
	EXPECT_FALSE(game_layout::ReadRendererLayout(code));
}

/// The message code with the call of the lookup at 0x3000 + 16. The call goes to 0x4000.
std::vector<std::byte> MessageCode()
{
	auto code = Code({{0x48, 0x8B, 0xD9, 0x48, 0x83, 0xC1, 0x28, 0x48, 0x8D, 0x55, 0x77, 0xE8, 0x00, 0x00, 0x00, 0x00}});
	const auto displacement = static_cast<std::uint32_t>(0x4000 - (0x3000 + 16 + 16));
	for (std::size_t index = 0; index < 4; ++index)
	{
		code[16 + 12 + index] = static_cast<std::byte>((displacement >> (8 * index)) & 0xFF);
	}
	return code;
}

// The read of the object id and the call of the manager in the function that finds the object.
const std::vector<std::byte> k_LookupCode = [] {
	auto code = Code({{0x48, 0x8B, 0x00}, {0x48, 0x8B, 0x57, 0x08, 0xFF, 0x50, 0x18}});
	code.resize(game_layout::k_ObjectLookupWindow, std::byte {0xCC});
	return code;
}();

const game_layout::CodeAt k_LookupAt = [](std::uintptr_t address, std::size_t size) -> std::span<const std::byte> {
	if (address == 0x4000 && size == k_LookupCode.size())
	{
		return k_LookupCode;
	}
	return {};
};

TEST(VideoLayout, ReadsTheChannelLayout)
{
	const auto layout =
	    game_layout::ReadChannelLayout(Code({k_ComponentChannel}), MessageCode(), 0x3000, Code({k_ChannelClipKey}), k_LookupAt);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->componentChannel, 0x20U);
	// The key of the object is at 0x28 in the message, and the id is 8 bytes into the key.
	EXPECT_EQ(layout->messageObjectId, 0x30U);
	EXPECT_EQ(layout->channelClipKey, 0xA0U);
}

TEST(VideoLayout, RefusesAChannelLayoutWithoutTheLookup)
{
	const game_layout::CodeAt none = [](std::uintptr_t, std::size_t) { return std::span<const std::byte> {}; };
	EXPECT_FALSE(
	    game_layout::ReadChannelLayout(Code({k_ComponentChannel}), MessageCode(), 0x3000, Code({k_ChannelClipKey}), none));
	EXPECT_FALSE(game_layout::ReadChannelLayout(Code({k_ComponentChannel}), Code({k_MessageObjectId}), 0x3000,
	                                            Code({k_ChannelClipKey}), k_LookupAt));
}

TEST(VideoLayout, ReadsTheListenerLayout)
{
	const auto offset = game_layout::ReadListenerLayout(Code({k_ListenerMatrix}));
	ASSERT_TRUE(offset) << offset.GetError().message;
	EXPECT_EQ(*offset, 0x40U);
}

TEST(VideoLayout, TheSignaturesHaveTheirFullLength)
{
	EXPECT_EQ(game_layout::k_RenderFrameSignature.size, 27U);
	EXPECT_EQ(game_layout::k_SetPlaylistSignature.size, 21U);
	EXPECT_EQ(game_layout::k_PlaylistMessageSignature.size, 24U);
	EXPECT_EQ(game_layout::k_ChannelUpdateSignature.size, 29U);
	EXPECT_EQ(game_layout::k_ListenerSignature.size, 12U);
}

TEST(CodeOffsets, ReadsTheTargetOfARelativeCall)
{
	// The call at index 2 of this code goes 0x10 bytes past the end of the call.
	const std::vector<std::byte> code {std::byte {0xCC}, std::byte {0xCC}, std::byte {0xE8}, std::byte {0x10},
	                                   std::byte {0x00}, std::byte {0x00}, std::byte {0x00}, std::byte {0xC3}};
	const RelativeRule rule {.name = "call", .pattern = "E8 ?? ?? ?? ?? C3", .at = 1, .end = 5};
	const auto target = ReadRelativeTarget(code, 0x1000, rule);
	ASSERT_TRUE(target) << target.GetError().message;
	EXPECT_EQ(*target, 0x1000u + 7 + 0x10);
	const RelativeRule bad {.name = "bad", .pattern = "E8 ?? ?? C3", .at = 1, .end = 3};
	EXPECT_FALSE(ReadRelativeTarget(code, 0x1000, bad));
}

TEST(CodeOffsets, ReadsANegativeRelativeTarget)
{
	const std::vector<std::byte> code {std::byte {0xE8}, std::byte {0xF0}, std::byte {0xFF},
	                                   std::byte {0xFF}, std::byte {0xFF}, std::byte {0xC3}};
	const RelativeRule rule {.name = "call", .pattern = "E8 ?? ?? ?? ?? C3", .at = 1, .end = 5};
	const auto target = ReadRelativeTarget(code, 0x1000, rule);
	ASSERT_TRUE(target);
	EXPECT_EQ(*target, 0x1000u + 5 - 0x10);
}

TEST(VideoLayout, ReadsThePlaceOfTheVideoSettings)
{
	// The two calls and the test of the copy flag, at the address 0x5000. The first call goes to 0x7000, the second
	// to 0x8000.
	auto code =
	    Code({{0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x80, 0x78, 0x01, 0x00, 0x74}});
	const std::uintptr_t base = 0x5000;
	const std::size_t call = 16;
	const auto put = [&](std::size_t at, std::int32_t value) {
		for (std::size_t index = 0; index < 4; ++index)
		{
			code[at + index] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (8 * index)) & 0xFF);
		}
	};
	put(call + 1, static_cast<std::int32_t>(0x7000 - (base + call + 5)));
	put(call + 9, static_cast<std::int32_t>(0x8000 - (base + call + 13)));
	// The getter of the manager reads the global at 0x9000. The getter of the settings adds 0x128.
	const std::vector<std::byte> manager {std::byte {0x48}, std::byte {0x8B}, std::byte {0x05}, std::byte {0xF9},
	                                      std::byte {0x1F}, std::byte {0x00}, std::byte {0x00}, std::byte {0xC3}};
	const std::vector<std::byte> settings {std::byte {0x48}, std::byte {0x8D}, std::byte {0x81}, std::byte {0x28},
	                                       std::byte {0x01}, std::byte {0x00}, std::byte {0x00}, std::byte {0xC3}};
	const game_layout::CodeAt codeAt = [&](std::uintptr_t address, std::size_t size) -> std::span<const std::byte> {
		if (address == 0x7000 && size == manager.size())
		{
			return manager;
		}
		if (address == 0x8000 && size == settings.size())
		{
			return settings;
		}
		return {};
	};
	const auto layout = game_layout::ReadVideoSettingsLayout(code, base, codeAt);
	ASSERT_TRUE(layout) << layout.GetError().message;
	EXPECT_EQ(layout->managerGlobal, 0x7000u + 7 + 0x1FF9);
	EXPECT_EQ(layout->settingsOffset, 0x128u);
	const game_layout::CodeAt none = [](std::uintptr_t, std::size_t) { return std::span<const std::byte> {}; };
	EXPECT_FALSE(game_layout::ReadVideoSettingsLayout(code, base, none));
}

} // namespace
