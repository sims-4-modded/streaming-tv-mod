/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <atomic>

namespace ts4_mod::youtube_tv
{

/// The gate of one thread. The first thread that calls `Enter` becomes the thread of the gate. After that, `Enter`
/// accepts only that thread. The game Python calls the bridge on one thread, and that thread is not the main thread of
/// the extender, so the bridge keeps the thread of its first call.
class ThreadGate
{
public:
	/// The result of `Enter`.
	enum class Entry
	{
		First,    ///< The gate had no thread. This thread is now the thread of the gate.
		Accepted, ///< This thread is the thread of the gate.
		Refused,  ///< Another thread is the thread of the gate.
	};

	/// Let the thread `threadId` in, or refuse it. The id 0 is never a thread, and the gate refuses it. Any thread can
	/// call it.
	[[nodiscard]] Entry Enter(std::uint32_t threadId)
	{
		if (threadId == 0)
		{
			return Entry::Refused;
		}
		std::uint32_t expected = 0;
		if (_thread.compare_exchange_strong(expected, threadId))
		{
			return Entry::First;
		}
		return expected == threadId ? Entry::Accepted : Entry::Refused;
	}

	/// The thread of the gate, or 0 before the first call.
	[[nodiscard]] std::uint32_t Thread() const { return _thread.load(); }

private:
	std::atomic<std::uint32_t> _thread {0};
};

} // namespace ts4_mod::youtube_tv
