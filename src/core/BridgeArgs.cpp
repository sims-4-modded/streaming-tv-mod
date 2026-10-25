/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "BridgeArgs.h"

#include <cmath>
#include <cstring>

#include <algorithm>

namespace ts4_mod::youtube_tv::bridge
{

ts4::Result<std::string_view> ReadUrl(const char* url)
{
	if (url == nullptr)
	{
		return ts4::MakeError("the URL is null");
	}
	std::size_t length = 0;
	while (length <= k_MaxUrlBytes && url[length] != '\0')
	{
		const auto value = static_cast<unsigned char>(url[length]);
		if (value < 0x20 || value == 0x7F)
		{
			return ts4::MakeError("the URL has a control character at byte {}", length);
		}
		++length;
	}
	if (length > k_MaxUrlBytes)
	{
		return ts4::MakeError("the URL is longer than {} bytes", k_MaxUrlBytes);
	}
	if (length == 0)
	{
		return ts4::MakeError("the URL is empty");
	}
	return std::string_view(url, length);
}

ts4::Result<std::array<float, 3>> ReadPosition(const float* position3)
{
	if (position3 == nullptr)
	{
		return ts4::MakeError("the position is null");
	}
	std::array<float, 3> position {};
	std::memcpy(position.data(), position3, sizeof(position));
	for (const auto value : position)
	{
		if (!std::isfinite(value) || std::abs(value) > k_MaxCoordinate)
		{
			return ts4::MakeError("the position ({}, {}, {}) is not correct", position[0], position[1], position[2]);
		}
	}
	return position;
}

ts4::Result<std::array<float, 4>> ReadOrientation(const float* orientation4)
{
	if (orientation4 == nullptr)
	{
		return ts4::MakeError("the orientation is null");
	}
	std::array<float, 4> quaternion {};
	std::memcpy(quaternion.data(), orientation4, sizeof(quaternion));
	double sum = 0.0;
	for (const auto value : quaternion)
	{
		if (!std::isfinite(value))
		{
			return ts4::MakeError("the orientation has a value that is not finite");
		}
		sum += double {value} * value;
	}
	const auto length = std::sqrt(sum);
	if (length < 1.0e-3)
	{
		return ts4::MakeError("the orientation has the length 0");
	}
	for (auto& value : quaternion)
	{
		value = static_cast<float>(value / length);
	}
	return quaternion;
}

ts4::Result<ObjectTransform> ReadTransform(const float* position3, const float* orientation4)
{
	auto position = ReadPosition(position3);
	if (!position)
	{
		return position.GetError();
	}
	auto orientation = ReadOrientation(orientation4);
	if (!orientation)
	{
		return orientation.GetError();
	}
	return ObjectTransform {.position = *position, .orientation = *orientation};
}

ts4::Result<ScreenKey> ReadScreenKey(std::uint64_t instance, std::uint32_t type, std::uint32_t group)
{
	if (type == 0)
	{
		return ts4::MakeError("the type of the clip key is 0");
	}
	return ScreenKey {.instance = instance, .type = type, .group = group};
}

ts4::Result<SpeedArguments> ReadSpeed(std::uint32_t clockSpeedMode, double speedScale, std::uint32_t superSpeed)
{
	const auto mode = ToClockSpeedMode(clockSpeedMode);
	if (!mode)
	{
		return ts4::MakeError("the clock speed mode {} is not known", clockSpeedMode);
	}
	if (!std::isfinite(speedScale) || speedScale < 0.0)
	{
		return ts4::MakeError("the speed scale {} is not correct", speedScale);
	}
	if (superSpeed > 1)
	{
		return ts4::MakeError("the super speed flag {} is not 0 or 1", superSpeed);
	}
	return SpeedArguments {.mode = *mode, .scale = speedScale, .superSpeed = superSpeed == 1};
}

std::uint32_t CopyText(std::string_view text, char* buffer, std::uint32_t size)
{
	const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(text.size(), UINT32_MAX));
	if (buffer == nullptr || size == 0)
	{
		return length;
	}
	std::size_t count = std::min<std::size_t>(length, size - 1);
	// Do not divide a UTF-8 character: a byte of the form 10xxxxxx continues a character.
	while (count > 0 && count < text.size() && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80)
	{
		--count;
	}
	std::memcpy(buffer, text.data(), count);
	buffer[count] = '\0';
	return length;
}

} // namespace ts4_mod::youtube_tv::bridge
