/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <span>

#include <common/Result.h>
#include <core/Yuv.h>

/// Write an I420 frame of the helper into the three planes of a video frame of the game. The game then copies the
/// planes into its textures and shows them on the screen of the TV.
namespace ts4_mod::youtube_tv
{

/// One plane of a video frame of the game: `height` rows of `width` bytes, with `pitch` bytes from the start of one
/// row to the start of the next row. `data` holds the bytes that the writer can change.
struct GamePlane
{
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint64_t pitch = 0;
	std::span<std::uint8_t> data;
};

/// The planes Y, U and V of one video frame of the game. U and V have half the width and half the height of Y,
/// rounded up.
struct GameFrame
{
	std::array<GamePlane, 3> planes;
	/// The game shows the rows in the opposite order. Then the writer writes the first row of the image last.
	bool flip = false;
};

/// The limits of the size of a plane of the game that the writer accepts.
constexpr std::uint32_t k_MinGamePlaneSize = 1;
constexpr std::uint32_t k_MaxGamePlaneSize = 4096;

/// How the writer changes the size of an image.
enum class ScaleFilter
{
	Nearest,  ///< The nearest pixel of the source.
	Bilinear, ///< The mean of the four nearest pixels, with weights.
};

/// What the writer did.
enum class FrameWrite
{
	Copy,      ///< The sizes are the same: the writer copied each row.
	Letterbox, ///< The sizes are different: the writer scaled the image to fit, with black bars.
};

/// Check the planes of a frame of the game: the sizes are in the limits, the pitch is not less than the width, the
/// data holds each row, and U and V have half the size of Y.
[[nodiscard]] ts4::Result<void> CheckGameFrame(const GameFrame& frame);

/// The part of the frame of the game that shows the image, if the image keeps its aspect ratio. The other parts are
/// black bars. The position and the size are even, so that U and V start at a full pixel.
struct FitRect
{
	std::uint32_t x = 0;
	std::uint32_t y = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;

	[[nodiscard]] bool operator==(const FitRect&) const = default;
};

[[nodiscard]] FitRect FitImage(std::uint32_t imageWidth, std::uint32_t imageHeight, std::uint32_t frameWidth,
                               std::uint32_t frameHeight);

/// Write `image` into `frame`. If the sizes are the same, the writer copies each row. Else it scales the image to fit
/// the frame, keeps the aspect ratio, and makes black bars with the black of `range`. The writer never writes a byte
/// outside the rows of the planes of the frame. The result is an error, and the writer writes nothing, if a plane is
/// not correct.
[[nodiscard]] ts4::Result<FrameWrite> WriteI420Frame(const ConstI420Planes& image, const GameFrame& frame, ScaleFilter filter,
                                                     YuvRange range);

} // namespace ts4_mod::youtube_tv
