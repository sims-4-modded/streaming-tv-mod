/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "Host.h"

#include <charconv>

#include <format>

#include <core/HostProtocol.h>

#include "include/cef_app.h"
#include "include/wrapper/cef_helpers.h"

namespace ts4_mod::youtube_tv::host
{
namespace
{

/// The time that the helper waits for CEF to close the browsers after `quit`.
constexpr auto k_QuitTimeout = std::chrono::seconds(10);

} // namespace

Host::Host(EventWriter& events, HostOptions options, std::string pageTemplate)
    : _events(events)
    , _options(std::move(options))
    , _pageTemplate(std::move(pageTemplate))
    , _start(std::chrono::steady_clock::now())
{
	if (_options.parentPid)
	{
		_parent.reset(::OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, *_options.parentPid));
		if (!_parent)
		{
			_events.SendError(0, "host", std::format("OpenProcess of the parent failed: {}", LastErrorText()));
		}
	}
}

Host::~Host() = default;

std::uint64_t Host::NowUs() const
{
	return static_cast<std::uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _start).count());
}

void Host::Handle(const HostCommand& command)
{
	CEF_REQUIRE_UI_THREAD();
	if (command.type == CommandType::Quit)
	{
		RequestQuit();
		return;
	}
	if (_quitting)
	{
		_events.SendError(command.id, "command", "the helper stops");
		return;
	}
	if (command.type == CommandType::Open)
	{
		Open(command);
		return;
	}
	const auto found = _sessions.find(command.id);
	if (found == _sessions.end())
	{
		_events.SendError(command.id, "command", std::format("no video has the id {}", command.id));
		return;
	}
	auto& session = found->second;
	switch (command.type)
	{
	case CommandType::Play:
		session->Play();
		break;
	case CommandType::Pause:
		session->Pause();
		break;
	case CommandType::SetRate:
		session->SetRate(command.rate);
		break;
	case CommandType::Seek:
		session->Seek(command.seconds);
		break;
	case CommandType::Close:
		session->Close();
		break;
	default:
		break;
	}
}

void Host::Open(const HostCommand& command)
{
	if (_sessions.contains(command.id))
	{
		_events.SendError(command.id, "open", std::format("a video with the id {} is open", command.id));
		return;
	}
	auto session = VideoSession::Make(*this, command);
	if (!session)
	{
		_events.SendError(command.id, "open", session.GetError().message);
		return;
	}
	{
		const std::lock_guard lock(_pagesMutex);
		_pages[command.id] = (*session)->Page();
	}
	_sessions[command.id] = *session;
	if (auto started = (*session)->Start(); !started)
	{
		_events.SendError(command.id, "open", started.GetError().message);
		_sessions.erase(command.id);
		const std::lock_guard lock(_pagesMutex);
		_pages.erase(command.id);
	}
}

void Host::OnSessionClosed(std::int32_t id)
{
	CEF_REQUIRE_UI_THREAD();
	_sessions.erase(id);
	{
		const std::lock_guard lock(_pagesMutex);
		_pages.erase(id);
	}
	_events.Send("closed", id);
	if (_quitting && _sessions.empty())
	{
		CefQuitMessageLoop();
	}
}

void Host::RequestQuit()
{
	CEF_REQUIRE_UI_THREAD();
	if (_quitting)
	{
		return;
	}
	_quitting = true;
	_watchdog = std::jthread([this](std::stop_token stop) {
		std::unique_lock lock(_watchdogMutex);
		if (!_watchdogStop.wait_for(lock, stop, k_QuitTimeout, [] { return false; }) && !stop.stop_requested())
		{
			// CEF did not stop in time. Stop the process, so that no helper stays after its parent.
			::TerminateProcess(::GetCurrentProcess(), 3);
		}
	});
	if (_sessions.empty())
	{
		CefQuitMessageLoop();
		return;
	}
	// Close() can remove the session from the map, thus copy the sessions first.
	std::vector<CefRefPtr<VideoSession>> sessions;
	for (const auto& [id, session] : _sessions)
	{
		sessions.push_back(session);
	}
	for (const auto& session : sessions)
	{
		session->Close();
	}
}

std::optional<std::string> Host::PageFor(std::int32_t id) const
{
	const std::lock_guard lock(_pagesMutex);
	const auto found = _pages.find(id);
	if (found == _pages.end())
	{
		return std::nullopt;
	}
	return found->second;
}

} // namespace ts4_mod::youtube_tv::host
