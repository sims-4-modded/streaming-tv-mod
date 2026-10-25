/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <condition_variable>

#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace ts4_mod::youtube_tv
{

/// A queue of lines from many threads to one writer thread. `Push` never waits for the writer, so a thread of the
/// game can send a command to the helper while it holds the lock of the runtime. The writer waits in `Pop`.
class LineQueue
{
public:
	/// Add a line. The result is false after `Close`.
	bool Push(std::string line)
	{
		{
			const std::lock_guard lock(_mutex);
			if (_closed)
			{
				return false;
			}
			_lines.push_back(std::move(line));
		}
		_ready.notify_one();
		return true;
	}

	/// Wait for the next line. After `Close`, give the lines that wait, then nothing.
	[[nodiscard]] std::optional<std::string> Pop()
	{
		std::unique_lock lock(_mutex);
		_ready.wait(lock, [this] { return _closed || !_lines.empty(); });
		if (_lines.empty())
		{
			return std::nullopt;
		}
		auto line = std::move(_lines.front());
		_lines.pop_front();
		return line;
	}

	/// Refuse new lines and wake the writer. The lines that wait stay in the queue for the writer.
	void Close()
	{
		{
			const std::lock_guard lock(_mutex);
			_closed = true;
		}
		_ready.notify_all();
	}

	[[nodiscard]] bool Closed() const
	{
		const std::lock_guard lock(_mutex);
		return _closed;
	}

private:
	mutable std::mutex _mutex;
	std::condition_variable _ready;
	std::deque<std::string> _lines;
	bool _closed = false;
};

} // namespace ts4_mod::youtube_tv
