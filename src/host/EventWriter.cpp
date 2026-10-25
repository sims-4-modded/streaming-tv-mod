/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "EventWriter.h"

#include <cstdio>

#include <string>

#include <core/HostCommand.h>

namespace ts4_mod::youtube_tv::host
{

EventWriter::EventWriter()
{
	HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
	HANDLE copy = nullptr;
	if (output != nullptr && output != INVALID_HANDLE_VALUE &&
	    ::DuplicateHandle(::GetCurrentProcess(), output, ::GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		_output.reset(copy);
	}
	// Other code in the process (CEF, the C runtime) must not write into the pipe of the events.
	HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (nul != INVALID_HANDLE_VALUE)
	{
		::SetStdHandle(STD_OUTPUT_HANDLE, nul);
		FILE* ignored = nullptr;
		::freopen_s(&ignored, "NUL", "w", stdout);
	}
}

void EventWriter::Send(std::string_view type, std::int32_t id, nlohmann::json data)
{
	auto line = EventLine(type, id, std::move(data));
	line += '\n';
	const std::lock_guard lock(_mutex);
	if (!_output || _broken)
	{
		return;
	}
	std::size_t done = 0;
	while (done < line.size())
	{
		DWORD written = 0;
		if (!::WriteFile(_output.get(), line.data() + done, static_cast<DWORD>(line.size() - done), &written, nullptr) ||
		    written == 0)
		{
			_broken = true;
			return;
		}
		done += written;
	}
}

void EventWriter::SendError(std::int32_t id, std::string_view source, std::string_view message, int code)
{
	nlohmann::json data {{"source", source}, {"message", message}};
	if (code != 0)
	{
		data["code"] = code;
	}
	Send("error", id, std::move(data));
}

} // namespace ts4_mod::youtube_tv::host
