/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "HostCommand.h"

#include <array>
#include <utility>

#include <core/HostProtocol.h>
#include <core/YoutubeUrl.h>

namespace ts4_mod::youtube_tv::host
{
namespace
{

constexpr std::array k_CommandNames {
    std::pair {CommandType::Open, "open"},       std::pair {CommandType::Play, "play"}, std::pair {CommandType::Pause, "pause"},
    std::pair {CommandType::SetRate, "setRate"}, std::pair {CommandType::Seek, "seek"}, std::pair {CommandType::Close, "close"},
    std::pair {CommandType::Quit, "quit"},
};

[[nodiscard]] ts4::Result<double> NumberField(const nlohmann::json& json, const char* name)
{
	const auto field = json.find(name);
	if (field == json.end() || !field->is_number())
	{
		return ts4::MakeError("the field \"{}\" is missing or is not a number", name);
	}
	return field->get<double>();
}

[[nodiscard]] ts4::Result<std::uint32_t> IntegerField(const nlohmann::json& json, const char* name, std::uint32_t min,
                                                      std::uint32_t max)
{
	const auto field = json.find(name);
	if (field == json.end() || !field->is_number_integer())
	{
		return ts4::MakeError("the field \"{}\" is missing or is not an integer", name);
	}
	const auto value = field->get<std::int64_t>();
	if (value < min || value > max)
	{
		return ts4::MakeError("the field \"{}\" is {}, but it must be from {} to {}", name, value, min, max);
	}
	return static_cast<std::uint32_t>(value);
}

[[nodiscard]] ts4::Result<void> ReadOpen(const nlohmann::json& json, HostCommand& command)
{
	std::string text;
	if (const auto field = json.find("videoId"); field != json.end() && field->is_string())
	{
		text = field->get<std::string>();
		if (!IsVideoId(text))
		{
			return ts4::MakeError("the video id \"{}\" is not correct", text);
		}
	}
	else if (const auto url = json.find("url"); url != json.end() && url->is_string())
	{
		text = url->get<std::string>();
	}
	else
	{
		return ts4::MakeError("the command \"open\" needs the text field \"videoId\" or \"url\"");
	}
	auto video = ParseYoutubeVideo(text);
	if (!video)
	{
		return video.GetError();
	}
	command.videoId = video->id;
	command.startSeconds = video->startSeconds;

	auto width = IntegerField(json, "width", k_MinVideoSize, k_MaxVideoSize);
	if (!width)
	{
		return width.GetError();
	}
	auto height = IntegerField(json, "height", k_MinVideoSize, k_MaxVideoSize);
	if (!height)
	{
		return height.GetError();
	}
	command.width = RoundVideoSize(*width);
	command.height = RoundVideoSize(*height);

	if (json.contains("start"))
	{
		auto start = IntegerField(json, "start", 0, 24U * 60U * 60U * 100U);
		if (!start)
		{
			return start.GetError();
		}
		command.startSeconds = *start;
	}
	if (json.contains("fps"))
	{
		auto fps = IntegerField(json, "fps", k_MinFrameRate, k_MaxFrameRate);
		if (!fps)
		{
			return fps.GetError();
		}
		command.frameRate = *fps;
	}
	if (const auto field = json.find("matrix"); field != json.end())
	{
		const auto matrix = field->is_string() ? ParseYuvMatrix(field->get<std::string>()) : std::nullopt;
		if (!matrix)
		{
			return ts4::MakeError("the field \"matrix\" must be \"bt601\" or \"bt709\"");
		}
		command.matrix = *matrix;
	}
	if (const auto field = json.find("range"); field != json.end())
	{
		const auto range = field->is_string() ? ParseYuvRange(field->get<std::string>()) : std::nullopt;
		if (!range)
		{
			return ts4::MakeError("the field \"range\" must be \"limited\" or \"full\"");
		}
		command.range = *range;
	}
	return {};
}

} // namespace

const char* CommandName(CommandType type)
{
	for (const auto& [value, name] : k_CommandNames)
	{
		if (value == type)
		{
			return name;
		}
	}
	return "unknown";
}

ts4::Result<HostCommand> ParseHostCommand(std::string_view line)
{
	const auto json = nlohmann::json::parse(line, nullptr, false);
	if (json.is_discarded() || !json.is_object())
	{
		return ts4::MakeError("the command is not a JSON object");
	}
	const auto cmd = json.find("cmd");
	if (cmd == json.end() || !cmd->is_string())
	{
		return ts4::MakeError("the command does not have the text field \"cmd\"");
	}
	const auto name = cmd->get<std::string>();
	HostCommand command;
	bool known = false;
	for (const auto& [value, valueName] : k_CommandNames)
	{
		if (name == valueName)
		{
			command.type = value;
			known = true;
		}
	}
	if (!known)
	{
		return ts4::MakeError("the command \"{}\" is not known", name);
	}
	if (command.type == CommandType::Quit)
	{
		return command;
	}

	const auto id = json.find("id");
	if (id == json.end() || !id->is_number_integer() || id->get<std::int64_t>() < 1 || id->get<std::int64_t>() > INT32_MAX)
	{
		return ts4::MakeError("the command \"{}\" needs an integer field \"id\" of 1 or more", name);
	}
	command.id = id->get<std::int32_t>();

	switch (command.type)
	{
	case CommandType::Open:
		if (auto result = ReadOpen(json, command); !result)
		{
			return result.GetError();
		}
		break;
	case CommandType::SetRate:
	{
		auto rate = NumberField(json, "rate");
		if (!rate)
		{
			return rate.GetError();
		}
		if (!(*rate >= k_MinPlaybackRate && *rate <= k_MaxPlaybackRate))
		{
			return ts4::MakeError("the rate {} is not from {} to {}", *rate, k_MinPlaybackRate, k_MaxPlaybackRate);
		}
		command.rate = *rate;
		break;
	}
	case CommandType::Seek:
	{
		auto seconds = NumberField(json, "seconds");
		if (!seconds)
		{
			return seconds.GetError();
		}
		if (!(*seconds >= 0.0 && *seconds < 1.0e7))
		{
			return ts4::MakeError("the time {} is not correct", *seconds);
		}
		command.seconds = *seconds;
		break;
	}
	default:
		break;
	}
	return command;
}

std::string CommandLine(const HostCommand& command)
{
	nlohmann::json json {{"cmd", CommandName(command.type)}};
	if (command.type != CommandType::Quit)
	{
		json["id"] = command.id;
	}
	switch (command.type)
	{
	case CommandType::Open:
		json["videoId"] = command.videoId;
		json["width"] = command.width;
		json["height"] = command.height;
		json["start"] = command.startSeconds;
		json["fps"] = command.frameRate;
		json["matrix"] = YuvMatrixName(command.matrix);
		json["range"] = YuvRangeName(command.range);
		break;
	case CommandType::SetRate:
		json["rate"] = command.rate;
		break;
	case CommandType::Seek:
		json["seconds"] = command.seconds;
		break;
	default:
		break;
	}
	return json.dump();
}

ts4::Result<HostEvent> ParseHostEvent(std::string_view line)
{
	auto json = nlohmann::json::parse(line, nullptr, false);
	if (json.is_discarded() || !json.is_object())
	{
		return ts4::MakeError("the event is not a JSON object");
	}
	const auto type = json.find("event");
	if (type == json.end() || !type->is_string())
	{
		return ts4::MakeError("the event does not have the text field \"event\"");
	}
	HostEvent event {.type = type->get<std::string>()};
	if (const auto id = json.find("id"); id != json.end() && id->is_number_integer())
	{
		event.id = id->get<std::int32_t>();
	}
	event.data = std::move(json);
	return event;
}

std::string EventLine(std::string_view type, std::int32_t id, nlohmann::json data)
{
	if (!data.is_object())
	{
		data = nlohmann::json::object();
	}
	data["event"] = type;
	if (id != 0)
	{
		data["id"] = id;
	}
	return data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace ts4_mod::youtube_tv::host
