/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "BridgeArgs.h"

#include <charconv>
#include <cmath>
#include <cstring>

#include <algorithm>
#include <string>

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

ts4::Result<std::uint64_t> ReadObjectIdField(const nlohmann::json& params, std::optional<std::uint64_t> fallback)
{
	if (!params.is_object())
	{
		return ts4::MakeError("the parameters are not a JSON object");
	}
	const auto it = params.find("objectId");
	if (it == params.end() || it->is_null())
	{
		if (!fallback)
		{
			return ts4::MakeError("the parameter \"objectId\" is necessary");
		}
		return *fallback;
	}
	if (it->is_number_unsigned() || (it->is_number_integer() && it->get<std::int64_t>() >= 0))
	{
		return it->get<std::uint64_t>();
	}
	if (!it->is_string())
	{
		return ts4::MakeError("the parameter \"objectId\" is not a whole number of 0 or more, a text or \"any\"");
	}
	const auto text = it->get<std::string>();
	if (text == "any")
	{
		return k_AnyObject;
	}
	std::string_view digits = text;
	int base = 10;
	if (digits.starts_with("0x") || digits.starts_with("0X"))
	{
		digits.remove_prefix(2);
		base = 16;
	}
	std::uint64_t value = 0;
	const auto* end = digits.data() + digits.size();
	const auto [last, error] = std::from_chars(digits.data(), end, value, base);
	if (digits.empty() || error != std::errc {} || last != end)
	{
		return ts4::MakeError("the parameter \"objectId\" \"{}\" is not a number", text);
	}
	return value;
}

ts4::Result<ObjectTransform> ReadTransformFields(const nlohmann::json& params)
{
	const auto numbers = [&](const char* name, std::size_t count, std::span<float> out) -> ts4::Result<void> {
		const auto it = params.find(name);
		if (it == params.end() || !it->is_array() || it->size() != count ||
		    !std::ranges::all_of(*it, [](const nlohmann::json& value) { return value.is_number(); }))
		{
			return ts4::MakeError("the parameter \"{}\" is not {} numbers", name, count);
		}
		for (std::size_t index = 0; index < count; ++index)
		{
			out[index] = (*it)[index].get<float>();
		}
		return {};
	};
	if (!params.is_object())
	{
		return ts4::MakeError("the parameters are not a JSON object");
	}
	std::array<float, 3> position {};
	std::array<float, 4> orientation {0.0F, 0.0F, 0.0F, 1.0F};
	if (auto read = numbers("position", 3, position); !read)
	{
		return read.GetError();
	}
	if (params.contains("orientation"))
	{
		if (auto read = numbers("orientation", 4, orientation); !read)
		{
			return read.GetError();
		}
	}
	return ReadTransform(position.data(), orientation.data());
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
