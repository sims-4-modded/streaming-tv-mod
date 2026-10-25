/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstdlib>

#include <array>
#include <vector>

#include <core/Yuv.h>
#include <gtest/gtest.h>

using ts4_mod::youtube_tv::BgraImage;
using ts4_mod::youtube_tv::ConstI420Planes;
using ts4_mod::youtube_tv::ConvertBgraToI420;
using ts4_mod::youtube_tv::ConvertI420ToBgra;
using ts4_mod::youtube_tv::I420Planes;
using ts4_mod::youtube_tv::ParseYuvMatrix;
using ts4_mod::youtube_tv::ParseYuvRange;
using ts4_mod::youtube_tv::YuvMatrix;
using ts4_mod::youtube_tv::YuvRange;

namespace
{

/// A BGRA image and I420 planes of the same size.
struct Images
{
	Images(std::uint32_t w, std::uint32_t h)
	    : width(w)
	    , height(h)
	    , bgra(std::size_t {w} * h * 4)
	    , y(std::size_t {w} * h)
	    , u(std::size_t {w} * h / 4)
	    , v(std::size_t {w} * h / 4)
	{
	}

	void Set(std::uint32_t x, std::uint32_t row, std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		auto* pixel = bgra.data() + (std::size_t {row} * width + x) * 4;
		pixel[0] = b;
		pixel[1] = g;
		pixel[2] = r;
		pixel[3] = 255;
	}

	void Fill(std::uint8_t r, std::uint8_t g, std::uint8_t b)
	{
		for (std::uint32_t row = 0; row < height; ++row)
		{
			for (std::uint32_t x = 0; x < width; ++x)
			{
				Set(x, row, r, g, b);
			}
		}
	}

	[[nodiscard]] BgraImage Image() const { return {.pixels = bgra, .pitch = width * 4, .width = width, .height = height}; }

	[[nodiscard]] I420Planes Planes()
	{
		return {.y = y, .u = u, .v = v, .pitchY = width, .pitchUV = width / 2, .width = width, .height = height};
	}

	[[nodiscard]] ConstI420Planes ConstPlanes() const
	{
		return {.y = y, .u = u, .v = v, .pitchY = width, .pitchUV = width / 2, .width = width, .height = height};
	}

	bool Convert(YuvMatrix matrix, YuvRange range) { return ConvertBgraToI420(Image(), Planes(), matrix, range); }

	std::uint32_t width;
	std::uint32_t height;
	std::vector<std::uint8_t> bgra;
	std::vector<std::uint8_t> y;
	std::vector<std::uint8_t> u;
	std::vector<std::uint8_t> v;
};

struct Yuv
{
	int y;
	int u;
	int v;
};

/// Convert one colour on a 2x2 image and return the values.
[[nodiscard]] Yuv ConvertColour(std::uint8_t r, std::uint8_t g, std::uint8_t b, YuvMatrix matrix, YuvRange range)
{
	Images images(2, 2);
	images.Fill(r, g, b);
	EXPECT_TRUE(images.Convert(matrix, range));
	return {.y = images.y[0], .u = images.u[0], .v = images.v[0]};
}

void ExpectYuv(Yuv actual, Yuv expected, const char* what)
{
	EXPECT_NEAR(actual.y, expected.y, 1) << what;
	EXPECT_NEAR(actual.u, expected.u, 1) << what;
	EXPECT_NEAR(actual.v, expected.v, 1) << what;
}

} // namespace

TEST(Yuv, GivesTheKnownBt601LimitedValues)
{
	constexpr auto m = YuvMatrix::Bt601;
	constexpr auto r = YuvRange::Limited;
	ExpectYuv(ConvertColour(0, 0, 0, m, r), {16, 128, 128}, "black");
	ExpectYuv(ConvertColour(255, 255, 255, m, r), {235, 128, 128}, "white");
	ExpectYuv(ConvertColour(255, 0, 0, m, r), {81, 90, 240}, "red");
	ExpectYuv(ConvertColour(0, 255, 0, m, r), {145, 54, 34}, "green");
	ExpectYuv(ConvertColour(0, 0, 255, m, r), {41, 240, 110}, "blue");
}

TEST(Yuv, GivesTheKnownBt709LimitedValues)
{
	constexpr auto m = YuvMatrix::Bt709;
	constexpr auto r = YuvRange::Limited;
	ExpectYuv(ConvertColour(255, 0, 0, m, r), {63, 102, 240}, "red");
	ExpectYuv(ConvertColour(0, 255, 0, m, r), {173, 42, 26}, "green");
	ExpectYuv(ConvertColour(0, 0, 255, m, r), {32, 240, 118}, "blue");
}

TEST(Yuv, GivesTheKnownFullRangeValues)
{
	constexpr auto m = YuvMatrix::Bt601;
	constexpr auto r = YuvRange::Full;
	ExpectYuv(ConvertColour(0, 0, 0, m, r), {0, 128, 128}, "black");
	ExpectYuv(ConvertColour(255, 255, 255, m, r), {255, 128, 128}, "white");
	ExpectYuv(ConvertColour(255, 0, 0, m, r), {76, 85, 255}, "red");
}

TEST(Yuv, TakesTheMeanOfFourPixelsForTheChroma)
{
	// The left column is red and the right column is blue: U and V are the mean of red and blue.
	Images images(2, 2);
	images.Set(0, 0, 255, 0, 0);
	images.Set(0, 1, 255, 0, 0);
	images.Set(1, 0, 0, 0, 255);
	images.Set(1, 1, 0, 0, 255);
	ASSERT_TRUE(images.Convert(YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_NEAR(images.y[0], 81, 1);
	EXPECT_NEAR(images.y[1], 41, 1);
	EXPECT_NEAR(images.y[2], 81, 1);
	EXPECT_NEAR(images.y[3], 41, 1);
	EXPECT_NEAR(images.u[0], (90 + 240) / 2, 1);
	EXPECT_NEAR(images.v[0], (240 + 110) / 2, 1);
}

TEST(Yuv, PutsEachBlockIntoItsOwnChromaValue)
{
	// Four 2x2 blocks: black, white, red, blue.
	Images images(4, 4);
	const std::array<std::array<std::uint8_t, 3>, 4> colours {{{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 0, 255}}};
	for (std::uint32_t row = 0; row < 4; ++row)
	{
		for (std::uint32_t x = 0; x < 4; ++x)
		{
			const auto& colour = colours[(row / 2) * 2 + x / 2];
			images.Set(x, row, colour[0], colour[1], colour[2]);
		}
	}
	ASSERT_TRUE(images.Convert(YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_EQ(images.y[0], 16);
	EXPECT_EQ(images.y[2], 235);
	EXPECT_NEAR(images.y[8], 81, 1);
	EXPECT_NEAR(images.y[15], 41, 1);
	EXPECT_NEAR(images.v[2], 240, 1);
	EXPECT_NEAR(images.u[3], 240, 1);
	EXPECT_EQ(images.u[0], 128);
	EXPECT_EQ(images.u[1], 128);
}

TEST(Yuv, UsesTheLastPixelsIfTheImageIsSmaller)
{
	Images planes(4, 4);
	Images small(2, 2);
	small.Fill(255, 255, 255);
	ASSERT_TRUE(ConvertBgraToI420(small.Image(), planes.Planes(), YuvMatrix::Bt601, YuvRange::Limited));
	for (const auto value : planes.y)
	{
		EXPECT_EQ(value, 235);
	}
}

TEST(Yuv, GivesBackTheImageOfFlatBlocks)
{
	Images images(16, 16);
	for (std::uint32_t row = 0; row < 16; ++row)
	{
		for (std::uint32_t x = 0; x < 16; ++x)
		{
			// The same colour in each 2x2 block, so the chroma loses nothing.
			const auto bx = static_cast<std::uint8_t>((x / 2) * 32);
			const auto by = static_cast<std::uint8_t>((row / 2) * 32);
			images.Set(x, row, bx, by, static_cast<std::uint8_t>(255 - bx));
		}
	}
	for (const auto matrix : {YuvMatrix::Bt601, YuvMatrix::Bt709})
	{
		for (const auto range : {YuvRange::Limited, YuvRange::Full})
		{
			ASSERT_TRUE(images.Convert(matrix, range));
			std::vector<std::uint8_t> back(images.bgra.size());
			ASSERT_TRUE(ConvertI420ToBgra(images.ConstPlanes(), back, 16 * 4, matrix, range));
			int worst = 0;
			for (std::size_t index = 0; index < back.size(); ++index)
			{
				worst = std::max(worst, std::abs(int {back[index]} - int {images.bgra[index]}));
			}
			EXPECT_LE(worst, 4) << static_cast<int>(matrix) << " " << static_cast<int>(range);
		}
	}
}

TEST(Yuv, RefusesSizesThatAreNotCorrect)
{
	Images images(4, 4);
	auto planes = images.Planes();
	planes.width = 3;
	EXPECT_FALSE(ConvertBgraToI420(images.Image(), planes, YuvMatrix::Bt601, YuvRange::Limited));
	planes = images.Planes();
	planes.y = planes.y.first(10);
	EXPECT_FALSE(ConvertBgraToI420(images.Image(), planes, YuvMatrix::Bt601, YuvRange::Limited));
	auto image = images.Image();
	image.pixels = image.pixels.first(8);
	EXPECT_FALSE(ConvertBgraToI420(image, images.Planes(), YuvMatrix::Bt601, YuvRange::Limited));
	std::vector<std::uint8_t> small(8);
	EXPECT_FALSE(ConvertI420ToBgra(images.ConstPlanes(), small, 16, YuvMatrix::Bt601, YuvRange::Limited));
}

TEST(Yuv, ReadsTheNamesOfMatrixAndRange)
{
	EXPECT_EQ(ParseYuvMatrix("bt601"), YuvMatrix::Bt601);
	EXPECT_EQ(ParseYuvMatrix("bt709"), YuvMatrix::Bt709);
	EXPECT_FALSE(ParseYuvMatrix("BT601"));
	EXPECT_EQ(ParseYuvRange("limited"), YuvRange::Limited);
	EXPECT_EQ(ParseYuvRange("full"), YuvRange::Full);
	EXPECT_FALSE(ParseYuvRange("tv"));
}
