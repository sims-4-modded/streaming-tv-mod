/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "Yuv.h"

#include <cmath>
#include <cstddef>

#include <algorithm>

namespace ts4_mod::youtube_tv
{
namespace
{

/// The bits after the binary point of the integer coefficients.
constexpr int k_Shift = 14;

struct Weights
{
	double kr;
	double kb;
};

[[nodiscard]] Weights MatrixWeights(YuvMatrix matrix)
{
	return matrix == YuvMatrix::Bt709 ? Weights {.kr = 0.2126, .kb = 0.0722} : Weights {.kr = 0.299, .kb = 0.114};
}

/// The scale of Y and of U and V, and the value of Y for black.
struct RangeScale
{
	double y;
	double c;
	double yOffset;
};

[[nodiscard]] RangeScale ScaleOfRange(YuvRange range)
{
	return range == YuvRange::Full ? RangeScale {.y = 1.0, .c = 1.0, .yOffset = 0.0}
	                               : RangeScale {.y = 219.0 / 255.0, .c = 224.0 / 255.0, .yOffset = 16.0};
}

/// The integer coefficients of one conversion from R, G and B (0 to 255) to Y, U and V.
struct Coefficients
{
	int yr, yg, yb;
	int ur, ug, ub;
	int vr, vg, vb;
	int yOffset;
};

[[nodiscard]] int Fixed(double value)
{
	return static_cast<int>(std::lround(value * (1 << k_Shift)));
}

[[nodiscard]] Coefficients MakeCoefficients(YuvMatrix matrix, YuvRange range)
{
	const auto [kr, kb] = MatrixWeights(matrix);
	const double kg = 1.0 - kr - kb;
	const auto scale = ScaleOfRange(range);
	const double cb = scale.c / (2.0 * (1.0 - kb));
	const double cr = scale.c / (2.0 * (1.0 - kr));
	return {
	    .yr = Fixed(scale.y * kr),
	    .yg = Fixed(scale.y * kg),
	    .yb = Fixed(scale.y * kb),
	    .ur = Fixed(-cb * kr),
	    .ug = Fixed(-cb * kg),
	    .ub = Fixed(cb * (1.0 - kb)),
	    .vr = Fixed(cr * (1.0 - kr)),
	    .vg = Fixed(-cr * kg),
	    .vb = Fixed(-cr * kb),
	    .yOffset = static_cast<int>(scale.yOffset),
	};
}

[[nodiscard]] std::uint8_t Clamp8(int value)
{
	return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

[[nodiscard]] bool PlanesFit(std::size_t ySize, std::size_t uSize, std::size_t vSize, std::uint32_t pitchY,
                             std::uint32_t pitchUV, std::uint32_t width, std::uint32_t height)
{
	if (width == 0 || height == 0 || width % 2 != 0 || height % 2 != 0 || pitchY < width || pitchUV < width / 2)
	{
		return false;
	}
	const auto yNeeded = std::size_t {pitchY} * (height - 1) + width;
	const auto cNeeded = std::size_t {pitchUV} * (height / 2 - 1) + width / 2;
	return ySize >= yNeeded && uSize >= cNeeded && vSize >= cNeeded;
}

} // namespace

std::optional<YuvMatrix> ParseYuvMatrix(std::string_view text)
{
	if (text == "bt601")
	{
		return YuvMatrix::Bt601;
	}
	if (text == "bt709")
	{
		return YuvMatrix::Bt709;
	}
	return std::nullopt;
}

std::optional<YuvRange> ParseYuvRange(std::string_view text)
{
	if (text == "limited")
	{
		return YuvRange::Limited;
	}
	if (text == "full")
	{
		return YuvRange::Full;
	}
	return std::nullopt;
}

const char* YuvMatrixName(YuvMatrix matrix)
{
	return matrix == YuvMatrix::Bt709 ? "bt709" : "bt601";
}

const char* YuvRangeName(YuvRange range)
{
	return range == YuvRange::Full ? "full" : "limited";
}

bool ConvertBgraToI420(const BgraImage& image, const I420Planes& out, YuvMatrix matrix, YuvRange range)
{
	if (image.width == 0 || image.height == 0 || image.pitch < image.width * 4 ||
	    image.pixels.size() < std::size_t {image.pitch} * (image.height - 1) + std::size_t {image.width} * 4)
	{
		return false;
	}
	if (!PlanesFit(out.y.size(), out.u.size(), out.v.size(), out.pitchY, out.pitchUV, out.width, out.height))
	{
		return false;
	}
	const auto c = MakeCoefficients(matrix, range);
	constexpr int k_Round = 1 << (k_Shift - 1);
	const auto lastColumn = image.width - 1;
	const auto lastRow = image.height - 1;

	for (std::uint32_t row = 0; row < out.height; row += 2)
	{
		const std::uint8_t* source[2] = {
		    image.pixels.data() + std::size_t {image.pitch} * std::min(row, lastRow),
		    image.pixels.data() + std::size_t {image.pitch} * std::min(row + 1, lastRow),
		};
		std::uint8_t* yRow[2] = {
		    out.y.data() + std::size_t {out.pitchY} * row,
		    out.y.data() + std::size_t {out.pitchY} * (row + 1),
		};
		std::uint8_t* uRow = out.u.data() + std::size_t {out.pitchUV} * (row / 2);
		std::uint8_t* vRow = out.v.data() + std::size_t {out.pitchUV} * (row / 2);

		for (std::uint32_t column = 0; column < out.width; column += 2)
		{
			int sumR = 0;
			int sumG = 0;
			int sumB = 0;
			for (int dy = 0; dy < 2; ++dy)
			{
				for (std::uint32_t dx = 0; dx < 2; ++dx)
				{
					const std::uint8_t* pixel = source[dy] + std::size_t {std::min(column + dx, lastColumn)} * 4;
					const int b = pixel[0];
					const int g = pixel[1];
					const int r = pixel[2];
					yRow[dy][column + dx] = Clamp8(((c.yr * r + c.yg * g + c.yb * b + k_Round) >> k_Shift) + c.yOffset);
					sumR += r;
					sumG += g;
					sumB += b;
				}
			}
			// The sums are four times the mean: shift two more bits.
			constexpr int k_SumShift = k_Shift + 2;
			constexpr int k_SumRound = 1 << (k_SumShift - 1);
			uRow[column / 2] = Clamp8(((c.ur * sumR + c.ug * sumG + c.ub * sumB + k_SumRound) >> k_SumShift) + 128);
			vRow[column / 2] = Clamp8(((c.vr * sumR + c.vg * sumG + c.vb * sumB + k_SumRound) >> k_SumShift) + 128);
		}
	}
	return true;
}

bool ConvertI420ToBgra(const ConstI420Planes& planes, std::span<std::uint8_t> out, std::uint32_t outPitch, YuvMatrix matrix,
                       YuvRange range)
{
	if (!PlanesFit(planes.y.size(), planes.u.size(), planes.v.size(), planes.pitchY, planes.pitchUV, planes.width,
	               planes.height) ||
	    outPitch < planes.width * 4 ||
	    out.size() < std::size_t {outPitch} * (planes.height - 1) + std::size_t {planes.width} * 4)
	{
		return false;
	}
	const auto [kr, kb] = MatrixWeights(matrix);
	const double kg = 1.0 - kr - kb;
	const auto scale = ScaleOfRange(range);

	for (std::uint32_t row = 0; row < planes.height; ++row)
	{
		for (std::uint32_t column = 0; column < planes.width; ++column)
		{
			const double y = (planes.y[std::size_t {planes.pitchY} * row + column] - scale.yOffset) / scale.y;
			const auto chroma = std::size_t {planes.pitchUV} * (row / 2) + column / 2;
			const double u = (planes.u[chroma] - 128.0) / scale.c;
			const double v = (planes.v[chroma] - 128.0) / scale.c;
			const double r = y + 2.0 * (1.0 - kr) * v;
			const double b = y + 2.0 * (1.0 - kb) * u;
			const double g = (y - kr * r - kb * b) / kg;
			std::uint8_t* pixel = out.data() + std::size_t {outPitch} * row + std::size_t {column} * 4;
			pixel[0] = Clamp8(static_cast<int>(std::lround(b)));
			pixel[1] = Clamp8(static_cast<int>(std::lround(g)));
			pixel[2] = Clamp8(static_cast<int>(std::lround(r)));
			pixel[3] = 255;
		}
	}
	return true;
}

} // namespace ts4_mod::youtube_tv
