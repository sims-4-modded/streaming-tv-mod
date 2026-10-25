/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <span>
#include <string_view>

/// The change of BGRA pixels to I420 (planar YUV 4:2:0) and back. The game shows the video of a TV from three planes:
/// Y at full size, U and V at half the width and half the height.
namespace ts4_mod::youtube_tv
{

/// The matrix that gives Y, U and V from R, G and B.
enum class YuvMatrix : std::uint32_t
{
	Bt601 = 0, ///< SD video: Kr 0.299, Kb 0.114.
	Bt709 = 1, ///< HD video: Kr 0.2126, Kb 0.0722.
};

/// The range of the values of Y, U and V.
enum class YuvRange : std::uint32_t
{
	Limited = 0, ///< Y from 16 to 235, U and V from 16 to 240 (the range of video).
	Full = 1,    ///< Y, U and V from 0 to 255 (the range of JPEG).
};

[[nodiscard]] std::optional<YuvMatrix> ParseYuvMatrix(std::string_view text);
[[nodiscard]] std::optional<YuvRange> ParseYuvRange(std::string_view text);
[[nodiscard]] const char* YuvMatrixName(YuvMatrix matrix);
[[nodiscard]] const char* YuvRangeName(YuvRange range);

/// Three planes of an I420 image that the function can write. The width and the height are even.
struct I420Planes
{
	std::span<std::uint8_t> y;
	std::span<std::uint8_t> u;
	std::span<std::uint8_t> v;
	std::uint32_t pitchY = 0;  ///< The bytes from the start of one row of Y to the start of the next row.
	std::uint32_t pitchUV = 0; ///< The same for U and V.
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

/// Three planes of an I420 image that the function only reads.
struct ConstI420Planes
{
	std::span<const std::uint8_t> y;
	std::span<const std::uint8_t> u;
	std::span<const std::uint8_t> v;
	std::uint32_t pitchY = 0;
	std::uint32_t pitchUV = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

/// One BGRA image in memory (8 bits for each channel, the order of bytes B, G, R, A, the top row first).
struct BgraImage
{
	std::span<const std::uint8_t> pixels;
	std::uint32_t pitch = 0; ///< The bytes from the start of one row to the start of the next row.
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

/// Change a BGRA image to I420. Each U and V value comes from the mean of the 2x2 pixels that it covers. If the image
/// is smaller than the planes, the function uses the last column and the last row again. The function ignores alpha.
/// Returns false, and writes nothing, if a size or a span is not correct.
bool ConvertBgraToI420(const BgraImage& image, const I420Planes& out, YuvMatrix matrix, YuvRange range);

/// Change an I420 image back to BGRA (alpha 255). The probe uses it to save a frame as PNG. Returns false if a size or
/// a span is not correct.
bool ConvertI420ToBgra(const ConstI420Planes& planes, std::span<std::uint8_t> out, std::uint32_t outPitch, YuvMatrix matrix,
                       YuvRange range);

} // namespace ts4_mod::youtube_tv
