/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "YoutubeTvSettings.h"

#include <cmath>

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

#include <core/HostProtocol.h>

namespace ts4_mod::youtube_tv
{

namespace
{

/// Reads the fields of one JSON object and keeps the first error.
class FieldReader
{
public:
	explicit FieldReader(const nlohmann::json& json)
	    : _json(json)
	{
	}

	void Bool(const char* name, bool& out)
	{
		if (const auto* field = Find(name))
		{
			if (!field->is_boolean())
			{
				Fail("the field \"{}\" is not true or false", name);
				return;
			}
			out = field->get<bool>();
		}
	}

	void Text(const char* name, std::string& out)
	{
		if (const auto* field = Find(name))
		{
			if (!field->is_string())
			{
				Fail("the field \"{}\" is not a text", name);
				return;
			}
			out = field->get<std::string>();
		}
	}

	void Number(const char* name, double& out, double min, double max)
	{
		if (const auto* field = Find(name))
		{
			if (!field->is_number() || !std::isfinite(field->get<double>()))
			{
				Fail("the field \"{}\" is not a number", name);
				return;
			}
			const auto value = field->get<double>();
			if (value < min || value > max)
			{
				Fail("the field \"{}\" is {}, but it must be from {} to {}", name, value, min, max);
				return;
			}
			out = value;
		}
	}

	void Count(const char* name, std::uint32_t& out, std::uint32_t min, std::uint32_t max)
	{
		if (const auto* field = Find(name))
		{
			if (!field->is_number_unsigned() && !(field->is_number_integer() && field->get<std::int64_t>() >= 0))
			{
				Fail("the field \"{}\" is not a whole number", name);
				return;
			}
			const auto value = field->get<std::uint64_t>();
			if (value < min || value > max)
			{
				Fail("the field \"{}\" is {}, but it must be from {} to {}", name, value, min, max);
				return;
			}
			out = static_cast<std::uint32_t>(value);
		}
	}

	template <typename T, typename Parse>
	void Choice(const char* name, T& out, Parse parse)
	{
		if (const auto* field = Find(name))
		{
			const auto value = field->is_string() ? parse(field->get<std::string>()) : std::optional<T> {};
			if (!value)
			{
				Fail("the field \"{}\" has a value that the mod does not know", name);
				return;
			}
			out = *value;
		}
	}

	void Vector(const char* name, Vec3& out)
	{
		if (const auto* field = Find(name))
		{
			if (!field->is_array() || field->size() != 3 ||
			    !std::ranges::all_of(*field, [](const nlohmann::json& value) { return value.is_number(); }))
			{
				Fail("the field \"{}\" is not three numbers", name);
				return;
			}
			const Vec3 value {(*field)[0].get<float>(), (*field)[1].get<float>(), (*field)[2].get<float>()};
			if (!(Length(value) > 0.0F) || !std::isfinite(Length(value)))
			{
				Fail("the field \"{}\" has the length 0", name);
				return;
			}
			out = value;
		}
	}

	[[nodiscard]] const std::optional<ts4::Error>& Error() const { return _error; }

private:
	[[nodiscard]] const nlohmann::json* Find(const char* name) const
	{
		if (_error)
		{
			return nullptr;
		}
		const auto it = _json.find(name);
		return it == _json.end() ? nullptr : &*it;
	}

	template <typename... Args>
	void Fail(std::format_string<Args...> format, Args&&... args)
	{
		_error = ts4::MakeError(format, std::forward<Args>(args)...);
	}

	const nlohmann::json& _json;
	std::optional<ts4::Error> _error;
};

[[nodiscard]] std::optional<ScaleFilter> ParseScaleFilter(std::string_view text)
{
	if (text == "nearest")
	{
		return ScaleFilter::Nearest;
	}
	if (text == "bilinear")
	{
		return ScaleFilter::Bilinear;
	}
	return std::nullopt;
}

[[nodiscard]] nlohmann::json VectorJson(Vec3 value)
{
	return nlohmann::json::array({value.x, value.y, value.z});
}

} // namespace

ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettings(const nlohmann::json& json)
{
	if (!json.is_object())
	{
		return ts4::MakeError("the settings are not a JSON object");
	}
	YoutubeTvSettings settings;
	FieldReader reader(json);
	reader.Bool("enabled", settings.enabled);
	reader.Text("helperPath", settings.helperPath);
	reader.Number("volume", settings.volume, 0.0, 1.0);
	reader.Number("audioRange", settings.audioRange, 0.1, k_MaxAudioRange);
	reader.Count("maxSessions", settings.maxSessions, 1, k_MaxSessions);
	reader.Count("width", settings.width, host::k_MinVideoSize, k_MaxSettingsVideoSize);
	reader.Count("height", settings.height, host::k_MinVideoSize, k_MaxSettingsVideoSize);
	reader.Count("frameRate", settings.frameRate, host::k_MinFrameRate, host::k_MaxFrameRate);
	reader.Choice("matrix", settings.matrix, ParseYuvMatrix);
	reader.Choice("range", settings.range, ParseYuvRange);
	reader.Choice("scaleFilter", settings.scaleFilter, ParseScaleFilter);
	reader.Choice("frameWrite", settings.frameWrite, ParseFrameWriteMode);
	reader.Number("speed2Rate", settings.speed.speed2Rate, host::k_MinPlaybackRate, host::k_MaxPlaybackRate);
	reader.Number("speed3Rate", settings.speed.speed3Rate, host::k_MinPlaybackRate, host::k_MaxPlaybackRate);
	reader.Count("muteLevel", settings.speed.muteLevel, 1, k_NeverMuteLevel);
	reader.Bool("rightHanded", settings.axes.rightHanded);
	reader.Choice("cameraMatrix", settings.axes.cameraLayout, ParseCameraMatrixLayout);
	reader.Bool("cameraLooksAlongNegativeZ", settings.axes.cameraLooksAlongNegativeZ);
	reader.Vector("modelFront", settings.axes.modelFront);
	reader.Vector("modelTop", settings.axes.modelTop);
	if (reader.Error())
	{
		return *reader.Error();
	}
	if (!Orthonormal({}, settings.axes.modelFront, settings.axes.modelTop))
	{
		return ts4::MakeError("the fields \"modelFront\" and \"modelTop\" are parallel");
	}
	return settings;
}

ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettingsText(std::string_view text)
{
	if (text.find_first_not_of(" \t\r\n") == std::string_view::npos)
	{
		return YoutubeTvSettings {};
	}
	const auto json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded())
	{
		return ts4::MakeError("the settings are not correct JSON");
	}
	return ParseYoutubeTvSettings(json);
}

nlohmann::json YoutubeTvSettingsJson(const YoutubeTvSettings& settings)
{
	return {
	    {"enabled", settings.enabled},
	    {"helperPath", settings.helperPath},
	    {"volume", settings.volume},
	    {"audioRange", settings.audioRange},
	    {"maxSessions", settings.maxSessions},
	    {"width", settings.width},
	    {"height", settings.height},
	    {"frameRate", settings.frameRate},
	    {"matrix", YuvMatrixName(settings.matrix)},
	    {"range", YuvRangeName(settings.range)},
	    {"scaleFilter", settings.scaleFilter == ScaleFilter::Nearest ? "nearest" : "bilinear"},
	    {"frameWrite", FrameWriteModeName(settings.frameWrite)},
	    {"speed2Rate", settings.speed.speed2Rate},
	    {"speed3Rate", settings.speed.speed3Rate},
	    {"muteLevel", settings.speed.muteLevel},
	    {"rightHanded", settings.axes.rightHanded},
	    {"cameraMatrix", CameraMatrixLayoutName(settings.axes.cameraLayout)},
	    {"cameraLooksAlongNegativeZ", settings.axes.cameraLooksAlongNegativeZ},
	    {"modelFront", VectorJson(settings.axes.modelFront)},
	    {"modelTop", VectorJson(settings.axes.modelTop)},
	};
}

SessionSettings SessionSettingsOf(const YoutubeTvSettings& settings)
{
	return SessionSettings {.maxSessions = settings.maxSessions,
	                        .width = host::RoundVideoSize(settings.width),
	                        .height = host::RoundVideoSize(settings.height),
	                        .frameRate = settings.frameRate,
	                        .matrix = settings.matrix,
	                        .range = settings.range,
	                        .speed = settings.speed};
}

} // namespace ts4_mod::youtube_tv
