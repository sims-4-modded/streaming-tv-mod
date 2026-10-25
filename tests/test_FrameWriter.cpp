/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstdint>

#include <algorithm>
#include <vector>

#include <core/FrameWriter.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

constexpr std::uint8_t k_Guard = 0xEE;

/// An I420 image of the helper with a test pattern: Y is (x + 3y) mod 256, U is 50 + x, V is 200 - y.
struct Image
{
	std::uint32_t width;
	std::uint32_t height;
	std::uint32_t pitchY;
	std::uint32_t pitchUV;
	std::vector<std::uint8_t> y;
	std::vector<std::uint8_t> u;
	std::vector<std::uint8_t> v;

	Image(std::uint32_t w, std::uint32_t h, std::uint32_t padding = 0)
	    : width(w)
	    , height(h)
	    , pitchY(w + padding)
	    , pitchUV(w / 2 + padding)
	    , y(std::size_t {pitchY} * h, 0)
	    , u(std::size_t {pitchUV} * (h / 2), 0)
	    , v(std::size_t {pitchUV} * (h / 2), 0)
	{
		for (std::uint32_t row = 0; row < h; ++row)
		{
			for (std::uint32_t column = 0; column < w; ++column)
			{
				y[row * pitchY + column] = static_cast<std::uint8_t>(column + 3 * row);
			}
		}
		for (std::uint32_t row = 0; row < h / 2; ++row)
		{
			for (std::uint32_t column = 0; column < w / 2; ++column)
			{
				u[row * pitchUV + column] = static_cast<std::uint8_t>(50 + column);
				v[row * pitchUV + column] = static_cast<std::uint8_t>(200 - row);
			}
		}
	}

	[[nodiscard]] ConstI420Planes Planes() const
	{
		return {.y = y, .u = u, .v = v, .pitchY = pitchY, .pitchUV = pitchUV, .width = width, .height = height};
	}
};

/// The planes of a fake frame of the game. Each buffer has guard bytes after the rows and in the padding of each row.
struct FakeFrame
{
	struct Buffer
	{
		std::uint32_t width;
		std::uint32_t height;
		std::uint64_t pitch;
		std::vector<std::uint8_t> bytes;

		[[nodiscard]] std::uint8_t At(std::uint32_t column, std::uint32_t row) const { return bytes[row * pitch + column]; }

		/// True if no byte outside the rows changed.
		[[nodiscard]] bool GuardsKept() const
		{
			for (std::size_t index = 0; index < bytes.size(); ++index)
			{
				const auto row = index / pitch;
				const auto column = index % pitch;
				if ((row >= height || column >= width) && bytes[index] != k_Guard)
				{
					return false;
				}
			}
			return true;
		}
	};

	std::array<Buffer, 3> buffers;
	bool flip = false;

	FakeFrame(std::uint32_t w, std::uint32_t h, std::uint32_t padding)
	{
		const std::uint32_t cw = (w + 1) / 2;
		const std::uint32_t ch = (h + 1) / 2;
		buffers[0] = {w, h, w + padding, std::vector<std::uint8_t>((w + padding) * (h + 2), k_Guard)};
		buffers[1] = {cw, ch, cw + padding, std::vector<std::uint8_t>((cw + padding) * (ch + 2), k_Guard)};
		buffers[2] = {cw, ch, cw + padding, std::vector<std::uint8_t>((cw + padding) * (ch + 2), k_Guard)};
	}

	[[nodiscard]] GameFrame Frame()
	{
		GameFrame frame {.flip = flip};
		for (std::size_t index = 0; index < 3; ++index)
		{
			auto& buffer = buffers[index];
			frame.planes[index] = {.width = buffer.width, .height = buffer.height, .pitch = buffer.pitch, .data = buffer.bytes};
		}
		return frame;
	}

	[[nodiscard]] bool GuardsKept() const
	{
		return std::ranges::all_of(buffers, [](const Buffer& buffer) { return buffer.GuardsKept(); });
	}
};

TEST(FrameWriter, CopiesAFrameOfTheSameSizeWithADifferentPitch)
{
	const Image image(32, 16, 8);
	FakeFrame frame(32, 16, 16);
	const auto result = WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Bilinear, YuvRange::Limited);
	ASSERT_TRUE(result) << result.GetError().message;
	EXPECT_EQ(*result, FrameWrite::Copy);
	for (std::uint32_t row = 0; row < 16; ++row)
	{
		for (std::uint32_t column = 0; column < 32; ++column)
		{
			ASSERT_EQ(frame.buffers[0].At(column, row), image.y[row * image.pitchY + column]);
		}
	}
	EXPECT_EQ(frame.buffers[1].At(3, 2), 53);
	EXPECT_EQ(frame.buffers[2].At(3, 2), 198);
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, WritesTheRowsInTheOppositeOrderWhenTheFrameFlips)
{
	const Image image(16, 16);
	FakeFrame frame(16, 16, 0);
	frame.flip = true;
	ASSERT_TRUE(WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Nearest, YuvRange::Limited));
	EXPECT_EQ(frame.buffers[0].At(5, 15), image.y[5]);
	EXPECT_EQ(frame.buffers[0].At(5, 0), image.y[15 * image.pitchY + 5]);
	EXPECT_EQ(frame.buffers[2].At(0, 7), 200);
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, FitsAWideImageWithBarsAtTheTopAndTheBottom)
{
	EXPECT_EQ(FitImage(640, 360, 640, 480), (FitRect {.x = 0, .y = 60, .width = 640, .height = 360}));
	EXPECT_EQ(FitImage(640, 480, 640, 360), (FitRect {.x = 80, .y = 0, .width = 480, .height = 360}));
	EXPECT_EQ(FitImage(1280, 720, 640, 368), (FitRect {.x = 0, .y = 4, .width = 640, .height = 360}));
	EXPECT_EQ(FitImage(16, 16, 16, 16), (FitRect {.x = 0, .y = 0, .width = 16, .height = 16}));
	EXPECT_EQ(FitImage(0, 16, 16, 16), FitRect {});
}

TEST(FrameWriter, ScalesAnImageIntoALargerFrameWithBlackBars)
{
	const Image image(32, 16);
	FakeFrame frame(64, 64, 4);
	const auto result = WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Bilinear, YuvRange::Limited);
	ASSERT_TRUE(result) << result.GetError().message;
	EXPECT_EQ(*result, FrameWrite::Letterbox);
	// The image fills the width: 64x32 at the row 16.
	EXPECT_EQ(frame.buffers[0].At(10, 0), 16);
	EXPECT_EQ(frame.buffers[0].At(10, 15), 16);
	EXPECT_EQ(frame.buffers[0].At(10, 48), 16);
	EXPECT_EQ(frame.buffers[1].At(3, 0), 128);
	EXPECT_EQ(frame.buffers[2].At(3, 31), 128);
	// U changes from 50 to 65 from left to right.
	EXPECT_GE(frame.buffers[1].At(0, 12), 50);
	EXPECT_LE(frame.buffers[1].At(31, 12), 65);
	EXPECT_LT(frame.buffers[1].At(0, 12), frame.buffers[1].At(31, 12));
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, ScalesAConstantImageToTheSameConstant)
{
	Image image(48, 32);
	std::ranges::fill(image.y, 77);
	FakeFrame frame(20, 14, 3);
	ASSERT_TRUE(WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Bilinear, YuvRange::Full));
	const auto rect = FitImage(48, 32, 20, 14);
	for (std::uint32_t row = rect.y; row < rect.y + rect.height; ++row)
	{
		for (std::uint32_t column = rect.x; column < rect.x + rect.width; ++column)
		{
			ASSERT_EQ(frame.buffers[0].At(column, row), 77);
		}
	}
	// The full range has the black 0.
	if (rect.y > 0)
	{
		EXPECT_EQ(frame.buffers[0].At(0, 0), 0);
	}
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, NearestTakesTheNearestPixel)
{
	const Image image(4, 4);
	FakeFrame frame(8, 8, 0);
	ASSERT_TRUE(WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Nearest, YuvRange::Limited));
	// Two target pixels for each source pixel.
	EXPECT_EQ(frame.buffers[0].At(0, 0), image.y[0]);
	EXPECT_EQ(frame.buffers[0].At(7, 7), image.y[3 * image.pitchY + 3]);
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, WritesAFrameWithAnOddSize)
{
	const Image image(16, 16);
	FakeFrame frame(15, 9, 1);
	ASSERT_TRUE(WriteI420Frame(image.Planes(), frame.Frame(), ScaleFilter::Bilinear, YuvRange::Limited));
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, RefusesAPitchLessThanTheWidth)
{
	const Image image(16, 16);
	FakeFrame frame(16, 16, 0);
	auto game = frame.Frame();
	game.planes[0].pitch = 8;
	EXPECT_FALSE(WriteI420Frame(image.Planes(), game, ScaleFilter::Nearest, YuvRange::Limited));
	EXPECT_TRUE(frame.GuardsKept());
	EXPECT_EQ(frame.buffers[0].At(0, 0), k_Guard);
}

TEST(FrameWriter, RefusesDataThatDoesNotHoldTheRows)
{
	const Image image(16, 16);
	FakeFrame frame(16, 16, 0);
	auto game = frame.Frame();
	game.planes[1].data = game.planes[1].data.first(10);
	EXPECT_FALSE(WriteI420Frame(image.Planes(), game, ScaleFilter::Nearest, YuvRange::Limited));
	EXPECT_EQ(frame.buffers[0].At(0, 0), k_Guard);
}

TEST(FrameWriter, RefusesChromaPlanesOfTheWrongSize)
{
	FakeFrame frame(16, 16, 0);
	auto game = frame.Frame();
	game.planes[2].width = 16;
	EXPECT_FALSE(CheckGameFrame(game));
	game = frame.Frame();
	game.planes[0].width = 5000;
	EXPECT_FALSE(CheckGameFrame(game));
}

TEST(FrameWriter, RefusesAnImageWithSmallPlanes)
{
	Image image(16, 16);
	auto planes = image.Planes();
	planes.y = planes.y.first(100);
	FakeFrame frame(16, 16, 0);
	EXPECT_FALSE(WriteI420Frame(planes, frame.Frame(), ScaleFilter::Nearest, YuvRange::Limited));
	EXPECT_EQ(frame.buffers[0].At(0, 0), k_Guard);
}

TEST(FrameWriter, WritesColourBarsWithABandAtTheTop)
{
	FakeFrame frame(64, 32, 8);
	const auto result = WriteColourBars(frame.Frame(), YuvRange::Limited);
	ASSERT_TRUE(result) << result.GetError().message;
	// The band at the top is white.
	EXPECT_EQ(frame.buffers[0].At(60, 0), 235);
	EXPECT_EQ(frame.buffers[0].At(60, 3), 235);
	// Below the band: the first bar is white, the last bar is black.
	EXPECT_EQ(frame.buffers[0].At(0, 20), 235);
	EXPECT_EQ(frame.buffers[0].At(63, 20), 16);
	EXPECT_EQ(frame.buffers[1].At(31, 10), 128);
	// The second bar is yellow.
	EXPECT_EQ(frame.buffers[0].At(10, 20), 210);
	EXPECT_EQ(frame.buffers[1].At(5, 10), 16);
	EXPECT_EQ(frame.buffers[2].At(5, 10), 146);
	EXPECT_TRUE(frame.GuardsKept());
}

TEST(FrameWriter, PutsTheBandOfTheBarsAtTheBottomRowsWhenTheFrameFlips)
{
	FakeFrame frame(64, 32, 0);
	frame.flip = true;
	ASSERT_TRUE(WriteColourBars(frame.Frame(), YuvRange::Limited));
	EXPECT_EQ(frame.buffers[0].At(63, 31), 235);
	EXPECT_EQ(frame.buffers[0].At(63, 0), 16);
}

TEST(FrameWriter, WritesFullRangeBars)
{
	FakeFrame frame(16, 16, 0);
	ASSERT_TRUE(WriteColourBars(frame.Frame(), YuvRange::Full));
	EXPECT_EQ(frame.buffers[0].At(0, 0), 255);
	EXPECT_EQ(frame.buffers[0].At(15, 15), 0);
	EXPECT_EQ(frame.buffers[1].At(7, 7), 128);
}

TEST(FrameWriter, RefusesBarsForAPlaneThatIsNotCorrect)
{
	FakeFrame frame(16, 16, 0);
	auto game = frame.Frame();
	game.planes[1].width = 3;
	EXPECT_FALSE(WriteColourBars(game, YuvRange::Limited));
}

TEST(FrameWriter, GivesTheMeanOfEachPlane)
{
	FakeFrame frame(16, 8, 4);
	ASSERT_TRUE(WriteColourBars(frame.Frame(), YuvRange::Limited));
	const auto means = PlaneMeans(frame.Frame());
	// The guard bytes in the padding do not count. The band is 1 row of 8 and the bars have 2 columns each.
	const double bars = (235.0 + 210 + 170 + 145 + 106 + 81 + 41 + 16) / 8.0;
	EXPECT_NEAR(means[0], (235.0 + 7 * bars) / 8.0, 0.01);
	EXPECT_GT(means[1], 0.0);
	FakeFrame empty(16, 8, 0);
	auto game = empty.Frame();
	game.planes[2].height = 0;
	EXPECT_EQ(PlaneMeans(game)[2], 0.0);
}

TEST(FrameWriter, ReadsTheNamesOfTheWriteModes)
{
	EXPECT_EQ(ParseFrameWriteMode("video"), FrameWriteMode::Video);
	EXPECT_EQ(ParseFrameWriteMode("off"), FrameWriteMode::Off);
	EXPECT_EQ(ParseFrameWriteMode("bars"), FrameWriteMode::Bars);
	EXPECT_FALSE(ParseFrameWriteMode("Bars"));
	EXPECT_STREQ(FrameWriteModeName(FrameWriteMode::Off), "off");
}

} // namespace
