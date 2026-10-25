/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "FrameWriter.h"

#include <cstring>

#include <algorithm>
#include <vector>

namespace ts4_mod::youtube_tv
{

namespace
{

/// A view of one plane that the writer reads.
struct SourcePlane
{
	std::span<const std::uint8_t> data;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::size_t pitch = 0;
};

[[nodiscard]] constexpr std::uint32_t Half(std::uint32_t size)
{
	return (size + 1) / 2;
}

/// True if `size` bytes hold `height` rows of `width` bytes with the stride `pitch`.
[[nodiscard]] bool HoldsRows(std::size_t size, std::uint32_t width, std::uint32_t height, std::uint64_t pitch)
{
	if (width == 0 || height == 0 || pitch < width)
	{
		return false;
	}
	const auto needed = pitch * (std::uint64_t {height} - 1) + width;
	return needed <= size;
}

[[nodiscard]] ts4::Result<void> CheckImage(const ConstI420Planes& image)
{
	if (image.width < 2 || image.height < 2 || image.width > k_MaxGamePlaneSize || image.height > k_MaxGamePlaneSize)
	{
		return ts4::MakeError("the image has the size {}x{}", image.width, image.height);
	}
	if (!HoldsRows(image.y.size(), image.width, image.height, image.pitchY) ||
	    !HoldsRows(image.u.size(), Half(image.width), Half(image.height), image.pitchUV) ||
	    !HoldsRows(image.v.size(), Half(image.width), Half(image.height), image.pitchUV))
	{
		return ts4::MakeError("the planes of the image are too small for the size {}x{}", image.width, image.height);
	}
	return {};
}

/// The row of the plane where the writer puts the row `row` of the picture.
[[nodiscard]] std::size_t TargetRow(std::uint32_t row, std::uint32_t height, bool flip)
{
	return flip ? height - 1 - row : row;
}

void FillPlane(const GamePlane& plane, std::uint8_t value, bool flip)
{
	for (std::uint32_t row = 0; row < plane.height; ++row)
	{
		std::memset(plane.data.data() + TargetRow(row, plane.height, flip) * plane.pitch, value, plane.width);
	}
}

void CopyPlane(const SourcePlane& source, const GamePlane& plane, bool flip)
{
	for (std::uint32_t row = 0; row < plane.height; ++row)
	{
		std::memcpy(plane.data.data() + TargetRow(row, plane.height, flip) * plane.pitch,
		            source.data.data() + row * source.pitch, plane.width);
	}
}

/// One position in the source for one position in the target: the first index, and the weight of the next index from
/// 0 to 256.
struct Tap
{
	std::uint32_t index = 0;
	std::uint32_t weight = 0;
};

/// The source positions for each of `target` positions, when `source` positions fill `target` positions.
[[nodiscard]] std::vector<Tap> Taps(std::uint32_t source, std::uint32_t target, ScaleFilter filter)
{
	std::vector<Tap> taps(target);
	const double scale = static_cast<double>(source) / target;
	for (std::uint32_t index = 0; index < target; ++index)
	{
		// The centre of the target pixel in the coordinates of the source.
		const double centre = (index + 0.5) * scale - 0.5;
		if (filter == ScaleFilter::Nearest)
		{
			const auto nearest = static_cast<std::int64_t>(centre + 0.5);
			taps[index].index = static_cast<std::uint32_t>(std::clamp<std::int64_t>(nearest, 0, source - 1));
			continue;
		}
		const double clamped = std::clamp(centre, 0.0, static_cast<double>(source - 1));
		const auto first = static_cast<std::uint32_t>(clamped);
		taps[index].index = first;
		taps[index].weight = first + 1 < source ? static_cast<std::uint32_t>((clamped - first) * 256.0 + 0.5) : 0;
	}
	return taps;
}

/// Scale the source plane into the rectangle of the target plane. The writer writes only the rows and the columns of
/// the rectangle.
void ScalePlane(const SourcePlane& source, const GamePlane& plane, const FitRect& rect, ScaleFilter filter, bool flip)
{
	const auto columns = Taps(source.width, rect.width, filter);
	const auto rows = Taps(source.height, rect.height, filter);
	for (std::uint32_t row = 0; row < rect.height; ++row)
	{
		auto* target = plane.data.data() + TargetRow(rect.y + row, plane.height, flip) * plane.pitch + rect.x;
		const auto& rowTap = rows[row];
		const auto* top = source.data.data() + rowTap.index * source.pitch;
		const auto* bottom = rowTap.weight == 0 ? top : top + source.pitch;
		for (std::uint32_t column = 0; column < rect.width; ++column)
		{
			const auto& tap = columns[column];
			if (filter == ScaleFilter::Nearest)
			{
				target[column] = top[tap.index];
				continue;
			}
			const std::uint32_t next = tap.weight == 0 ? tap.index : tap.index + 1;
			const std::uint32_t upper = top[tap.index] * (256 - tap.weight) + top[next] * tap.weight;
			const std::uint32_t lower = bottom[tap.index] * (256 - tap.weight) + bottom[next] * tap.weight;
			const std::uint32_t value = upper * (256 - rowTap.weight) + lower * rowTap.weight;
			target[column] = static_cast<std::uint8_t>((value + 32768) >> 16);
		}
	}
}

} // namespace

ts4::Result<void> CheckGameFrame(const GameFrame& frame)
{
	const auto& y = frame.planes[0];
	if (y.width < k_MinGamePlaneSize || y.height < k_MinGamePlaneSize || y.width > k_MaxGamePlaneSize ||
	    y.height > k_MaxGamePlaneSize)
	{
		return ts4::MakeError("the Y plane of the game has the size {}x{}", y.width, y.height);
	}
	for (std::size_t index = 1; index < 3; ++index)
	{
		const auto& plane = frame.planes[index];
		if (plane.width != Half(y.width) || plane.height != Half(y.height))
		{
			return ts4::MakeError("plane {} of the game has the size {}x{} for a Y plane of {}x{}", index, plane.width,
			                      plane.height, y.width, y.height);
		}
	}
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto& plane = frame.planes[index];
		if (!HoldsRows(plane.data.size(), plane.width, plane.height, plane.pitch))
		{
			return ts4::MakeError("plane {} of the game has the pitch {} and {} bytes for the size {}x{}", index, plane.pitch,
			                      plane.data.size(), plane.width, plane.height);
		}
	}
	return {};
}

FitRect FitImage(std::uint32_t imageWidth, std::uint32_t imageHeight, std::uint32_t frameWidth, std::uint32_t frameHeight)
{
	if (imageWidth == 0 || imageHeight == 0 || frameWidth == 0 || frameHeight == 0)
	{
		return {};
	}
	// Compare the aspect ratios without a division: the image fills the width if it is wider than the frame.
	std::uint64_t width = frameWidth;
	std::uint64_t height = frameHeight;
	if (std::uint64_t {imageWidth} * frameHeight >= std::uint64_t {imageHeight} * frameWidth)
	{
		height = (std::uint64_t {frameWidth} * imageHeight + imageWidth / 2) / imageWidth;
	}
	else
	{
		width = (std::uint64_t {frameHeight} * imageWidth + imageHeight / 2) / imageHeight;
	}
	// Keep even sizes and positions, so that U and V start at a full pixel. A frame of one pixel keeps its size.
	const auto even = [](std::uint64_t value, std::uint32_t limit) {
		const auto rounded = value & ~std::uint64_t {1};
		return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounded == 0 ? value : rounded, 1, limit));
	};
	FitRect rect {.width = even(width, frameWidth), .height = even(height, frameHeight)};
	rect.x = ((frameWidth - rect.width) / 2) & ~1U;
	rect.y = ((frameHeight - rect.height) / 2) & ~1U;
	return rect;
}

ts4::Result<FrameWrite> WriteI420Frame(const ConstI420Planes& image, const GameFrame& frame, ScaleFilter filter, YuvRange range)
{
	if (auto checked = CheckImage(image); !checked)
	{
		return checked.GetError();
	}
	if (auto checked = CheckGameFrame(frame); !checked)
	{
		return checked.GetError();
	}
	const std::array<SourcePlane, 3> sources {
	    SourcePlane {.data = image.y, .width = image.width, .height = image.height, .pitch = image.pitchY},
	    SourcePlane {.data = image.u, .width = Half(image.width), .height = Half(image.height), .pitch = image.pitchUV},
	    SourcePlane {.data = image.v, .width = Half(image.width), .height = Half(image.height), .pitch = image.pitchUV},
	};
	const auto& y = frame.planes[0];
	if (y.width == image.width && y.height == image.height)
	{
		for (std::size_t index = 0; index < 3; ++index)
		{
			CopyPlane(sources[index], frame.planes[index], frame.flip);
		}
		return FrameWrite::Copy;
	}

	const auto rect = FitImage(image.width, image.height, y.width, y.height);
	const std::uint8_t blackY = range == YuvRange::Limited ? 16 : 0;
	constexpr std::uint8_t k_BlackUV = 128;
	// The rectangle of U and V: half of the rectangle of Y, in the limits of the smaller planes.
	const auto& u = frame.planes[1];
	FitRect chroma {.x = rect.x / 2, .y = rect.y / 2, .width = Half(rect.width), .height = Half(rect.height)};
	chroma.width = std::min(chroma.width, u.width - chroma.x);
	chroma.height = std::min(chroma.height, u.height - chroma.y);
	const std::array<FitRect, 3> rects {rect, chroma, chroma};
	const std::array<std::uint8_t, 3> black {blackY, k_BlackUV, k_BlackUV};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto& plane = frame.planes[index];
		if (rects[index].width != plane.width || rects[index].height != plane.height)
		{
			FillPlane(plane, black[index], frame.flip);
		}
		ScalePlane(sources[index], plane, rects[index], filter, frame.flip);
	}
	return FrameWrite::Letterbox;
}

std::optional<FrameWriteMode> ParseFrameWriteMode(std::string_view text)
{
	if (text == "video")
	{
		return FrameWriteMode::Video;
	}
	if (text == "off")
	{
		return FrameWriteMode::Off;
	}
	if (text == "bars")
	{
		return FrameWriteMode::Bars;
	}
	return std::nullopt;
}

const char* FrameWriteModeName(FrameWriteMode mode)
{
	switch (mode)
	{
	case FrameWriteMode::Video:
		return "video";
	case FrameWriteMode::Off:
		return "off";
	case FrameWriteMode::Bars:
		return "bars";
	}
	return "video";
}

ts4::Result<void> WriteColourBars(const GameFrame& frame, YuvRange range)
{
	if (auto checked = CheckGameFrame(frame); !checked)
	{
		return checked.GetError();
	}
	// The bars of BT.601 at full intensity, in the limited range: Y, U, V.
	constexpr std::array<std::array<std::uint8_t, 3>, 8> k_Bars {{
	    {235, 128, 128},
	    {210, 16, 146},
	    {170, 166, 16},
	    {145, 54, 34},
	    {106, 202, 222},
	    {81, 90, 240},
	    {41, 240, 110},
	    {16, 128, 128},
	}};
	const auto value = [range](std::uint8_t limited, bool luma) -> std::uint8_t {
		if (range == YuvRange::Limited)
		{
			return limited;
		}
		const double scale = luma ? 255.0 / 219.0 : 255.0 / 224.0;
		const double offset = luma ? 16.0 : 128.0;
		const double full = luma ? (limited - offset) * scale : (limited - offset) * scale + 128.0;
		return static_cast<std::uint8_t>(std::clamp(full + 0.5, 0.0, 255.0));
	};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto& plane = frame.planes[index];
		const auto band = std::max<std::uint32_t>(1, plane.height / 8);
		for (std::uint32_t row = 0; row < plane.height; ++row)
		{
			auto* target = plane.data.data() + TargetRow(row, plane.height, frame.flip) * plane.pitch;
			for (std::uint32_t column = 0; column < plane.width; ++column)
			{
				const auto bar = row < band ? 0 : std::min<std::size_t>(7, std::size_t {column} * 8 / plane.width);
				target[column] = value(k_Bars[bar][index], index == 0);
			}
		}
	}
	return {};
}

std::array<double, 3> PlaneMeans(const GameFrame& frame)
{
	std::array<double, 3> means {};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto& plane = frame.planes[index];
		if (plane.width == 0 || plane.height == 0 || !HoldsRows(plane.data.size(), plane.width, plane.height, plane.pitch))
		{
			continue;
		}
		std::uint64_t sum = 0;
		for (std::uint32_t row = 0; row < plane.height; ++row)
		{
			const auto* source = plane.data.data() + row * plane.pitch;
			for (std::uint32_t column = 0; column < plane.width; ++column)
			{
				sum += source[column];
			}
		}
		means[index] = static_cast<double>(sum) / (static_cast<double>(plane.width) * plane.height);
	}
	return means;
}

} // namespace ts4_mod::youtube_tv
