/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <condition_variable>
#include <cstdint>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <core/HostCommand.h>
#include <host/EventWriter.h>
#include <host/VideoSession.h>
#include <host/WinHandle.h>

namespace ts4_mod::youtube_tv::host
{

/// The values from the command line of the helper.
struct HostOptions
{
	std::optional<std::uint32_t> parentPid; ///< `--parent-pid`: the process that gets the shared handles.
	std::wstring cacheFolder;               ///< `--cache-dir`: the root cache folder of CEF.
};

/// The state of the helper. It lives on the stack of `wWinMain`, from before `CefInitialize` to after `CefShutdown`.
/// Only the UI thread of CEF calls its functions, except `PageFor`.
class Host
{
public:
	Host(EventWriter& events, HostOptions options, std::string pageTemplate);
	~Host();

	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;

	/// Do one command of the parent.
	void Handle(const HostCommand& command);

	/// Close all browsers, then stop the message loop. If CEF does not stop in 10 seconds, the helper stops itself.
	void RequestQuit();

	/// A session calls this from `OnBeforeClose`.
	void OnSessionClosed(std::int32_t id);

	/// The page of one session, or nothing. The IO thread of CEF calls it.
	[[nodiscard]] std::optional<std::string> PageFor(std::int32_t id) const;

	[[nodiscard]] EventWriter& Events() { return _events; }
	[[nodiscard]] const std::string& PageTemplate() const { return _pageTemplate; }
	/// The process of the parent with the right to get handles, or null if the parent did not give its id.
	[[nodiscard]] HANDLE ParentProcess() const { return _parent.get(); }
	/// Microseconds from the start of the helper.
	[[nodiscard]] std::uint64_t NowUs() const;

private:
	void Open(const HostCommand& command);

	EventWriter& _events;
	HostOptions _options;
	std::string _pageTemplate;
	UniqueHandle _parent;
	std::chrono::steady_clock::time_point _start;

	std::map<std::int32_t, CefRefPtr<VideoSession>> _sessions;
	mutable std::mutex _pagesMutex;
	std::map<std::int32_t, std::string> _pages;
	bool _quitting = false;

	std::mutex _watchdogMutex;
	std::condition_variable_any _watchdogStop;
	std::jthread _watchdog;
};

} // namespace ts4_mod::youtube_tv::host
