/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <string>
#include <string_view>

#include <common/Result.h>
#include <nlohmann/json.hpp>

/// The commands that the parent sends to the helper, and the events that the helper sends back. Each command and each
/// event is one JSON object on one line (UTF-8, `\n` at the end): the parent writes commands to the standard input of
/// the helper, and reads events from its standard output.
namespace ts4_mod::youtube_tv::host
{

enum class CommandType
{
	Open,
	Play,
	Pause,
	SetRate,
	Seek,
	Close,
	Quit,
};

/// How the helper gets the frames from CEF.
enum class PaintMode
{
	Gpu, ///< `OnAcceleratedPaint`: CEF gives a GPU texture, and the helper copies it on the GPU.
	Cpu, ///< `OnPaint`: CEF gives the pixels in memory, and the helper sends them to the GPU.
};

/// One command from the parent.
struct HostCommand
{
	CommandType type = CommandType::Quit;
	std::int32_t id = 0; ///< The video that the command is for. The parent selects the id in `open`.

	// `open` only.
	std::string videoId;
	std::uint32_t startSeconds = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t frameRate = 30;
	PaintMode paint = PaintMode::Gpu;

	double rate = 1.0;    ///< `setRate` only.
	double seconds = 0.0; ///< `seek` only.

	[[nodiscard]] bool operator==(const HostCommand&) const = default;
};

/// The limits of the frame rate of the browser.
constexpr std::uint32_t k_MinFrameRate = 1;
constexpr std::uint32_t k_MaxFrameRate = 60;

/// The name of a command in the field `cmd`.
[[nodiscard]] const char* CommandName(CommandType type);

/// Read one command line. The result is an error for a line that is not correct JSON, for an unknown command, and for
/// a missing field or a value out of its range. The `open` command takes `videoId` or `url` (any form that
/// `ParseYoutubeVideo` accepts).
[[nodiscard]] ts4::Result<HostCommand> ParseHostCommand(std::string_view line);

/// Write one command as a line of JSON, without the `\n`.
[[nodiscard]] std::string CommandLine(const HostCommand& command);

/// One event from the helper.
struct HostEvent
{
	std::string type;    ///< The field `event`, for example "ready" or "state".
	std::int32_t id = 0; ///< The video that the event is for, or 0 for an event of the helper.
	nlohmann::json data; ///< The full object of the event.
};

/// Read one event line. The result is an error if the line is not a JSON object with a text field `event`.
[[nodiscard]] ts4::Result<HostEvent> ParseHostEvent(std::string_view line);

/// Write one event as a line of JSON, without the `\n`. The function adds the fields `event` and `id` to `data`.
[[nodiscard]] std::string EventLine(std::string_view type, std::int32_t id, nlohmann::json data = nlohmann::json::object());

} // namespace ts4_mod::youtube_tv::host
