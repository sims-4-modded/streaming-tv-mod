/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <atomic>
#include <mutex>
#include <string_view>

#include <host/WinHandle.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::youtube_tv::host
{

/// Writes the event lines to the parent. All threads can use it: a lock keeps each line complete.
class EventWriter
{
public:
	/// Use the standard output that the process has at the start. The function gives the process a different
	/// standard output (`NUL`), so that only this object writes to the pipe of the parent.
	EventWriter();

	EventWriter(const EventWriter&) = delete;
	EventWriter& operator=(const EventWriter&) = delete;

	/// Write one event. If the parent closed the pipe, the function does nothing, and `Broken` becomes true.
	void Send(std::string_view type, std::int32_t id = 0, nlohmann::json data = nlohmann::json::object());

	/// Write an `error` event.
	void SendError(std::int32_t id, std::string_view source, std::string_view message, int code = 0);

	/// True after a write to the parent failed.
	[[nodiscard]] bool Broken() const { return _broken.load(); }

private:
	std::mutex _mutex;
	UniqueHandle _output;
	std::atomic<bool> _broken {false};
};

} // namespace ts4_mod::youtube_tv::host
