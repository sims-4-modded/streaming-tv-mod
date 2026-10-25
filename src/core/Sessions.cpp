/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "Sessions.h"

#include <algorithm>
#include <format>
#include <utility>

namespace ts4_mod::youtube_tv
{

const char* SessionStateName(SessionState state)
{
	switch (state)
	{
	case SessionState::Opening:
		return "opening";
	case SessionState::Loading:
		return "loading";
	case SessionState::Playing:
		return "playing";
	case SessionState::Paused:
		return "paused";
	case SessionState::Buffering:
		return "buffering";
	case SessionState::Ended:
		return "ended";
	case SessionState::Failed:
		return "failed";
	}
	return "unknown";
}

void SessionActions::Add(SessionActions&& other)
{
	commands.insert(commands.end(), other.commands.begin(), other.commands.end());
	released.insert(released.end(), other.released.begin(), other.released.end());
}

SessionManager::SessionManager(SessionSettings settings)
    : _settings(std::move(settings))
{
}

SessionActions SessionManager::SetSettings(SessionSettings settings)
{
	_settings = std::move(settings);
	return SetSpeed(_mode, _superSpeed);
}

SessionManager::KnownObject& SessionManager::Known(std::uint64_t objectId)
{
	if (_objects.size() >= k_MaxKnownObjects && !_objects.contains(objectId))
	{
		// Remove the objects that have no video, so that the map does not grow without a limit.
		std::erase_if(_objects, [this](const auto& entry) { return Find(entry.first) == nullptr; });
	}
	return _objects[objectId];
}

ts4::Result<SessionActions> SessionManager::Play(std::uint64_t objectId, std::string_view url,
                                                 std::optional<ObjectTransform> transform)
{
	auto video = ParseYoutubeVideo(url);
	if (!video)
	{
		return video.GetError();
	}
	SessionActions actions;
	if (const auto it = std::ranges::find(_sessions, objectId, &Session::objectId); it != _sessions.end())
	{
		actions.Add(Release(static_cast<std::size_t>(it - _sessions.begin())));
	}
	if (_sessions.size() >= _settings.maxSessions)
	{
		return ts4::MakeError("the limit of {} videos is full", _settings.maxSessions);
	}
	if (transform)
	{
		SetTransform(objectId, *transform);
	}
	Session session {.objectId = objectId, .helperId = _nextHelperId++, .video = *video};
	actions.commands.push_back(host::HostCommand {.type = host::CommandType::Open,
	                                              .id = session.helperId,
	                                              .videoId = video->id,
	                                              .startSeconds = video->startSeconds,
	                                              .width = _settings.width,
	                                              .height = _settings.height,
	                                              .frameRate = _settings.frameRate,
	                                              .matrix = _settings.matrix,
	                                              .range = _settings.range});
	_sessions.push_back(std::move(session));
	return actions;
}

SessionActions SessionManager::Release(std::size_t index)
{
	SessionActions actions;
	const auto helperId = _sessions[index].helperId;
	actions.commands.push_back(host::HostCommand {.type = host::CommandType::Close, .id = helperId});
	actions.released.push_back(helperId);
	_sessions.erase(_sessions.begin() + static_cast<std::ptrdiff_t>(index));
	return actions;
}

std::optional<SessionActions> SessionManager::Stop(std::uint64_t objectId)
{
	const auto it = std::ranges::find(_sessions, objectId, &Session::objectId);
	if (it == _sessions.end())
	{
		return std::nullopt;
	}
	return Release(static_cast<std::size_t>(it - _sessions.begin()));
}

SessionActions SessionManager::StopAll()
{
	SessionActions actions;
	while (!_sessions.empty())
	{
		actions.Add(Release(_sessions.size() - 1));
	}
	return actions;
}

Playback SessionManager::CurrentPlayback() const
{
	return SpeedPolicy(_mode, _superSpeed, _settings.speed);
}

SessionActions SessionManager::Apply(Session& session)
{
	SessionActions actions;
	if (!session.ready || session.state == SessionState::Failed)
	{
		return actions;
	}
	const auto playback = CurrentPlayback();
	if (playback.play)
	{
		if (session.sentRate != playback.rate)
		{
			actions.commands.push_back({.type = host::CommandType::SetRate, .id = session.helperId, .rate = playback.rate});
			session.sentRate = playback.rate;
		}
		if (session.sentPlay != true)
		{
			actions.commands.push_back({.type = host::CommandType::Play, .id = session.helperId});
		}
	}
	else if (session.sentPlay != false)
	{
		actions.commands.push_back({.type = host::CommandType::Pause, .id = session.helperId});
	}
	session.sentPlay = playback.play;
	return actions;
}

SessionActions SessionManager::SetSpeed(ClockSpeedMode mode, bool superSpeed)
{
	_mode = mode;
	_superSpeed = superSpeed;
	SessionActions actions;
	for (auto& session : _sessions)
	{
		actions.Add(Apply(session));
	}
	return actions;
}

void SessionManager::SetTransform(std::uint64_t objectId, const ObjectTransform& transform)
{
	Known(objectId).transform = transform;
}

void SessionManager::SetScreenKey(std::uint64_t objectId, const ScreenKey& key)
{
	Known(objectId).screenKey = key;
}

void SessionManager::BindChannel(std::uint64_t objectId, std::uintptr_t channel)
{
	if (channel == 0)
	{
		return;
	}
	// One channel belongs to one object. The game can use the memory of a removed channel for a new channel.
	for (auto& [id, known] : _objects)
	{
		if (id != objectId && known.channel == channel)
		{
			known.channel = 0;
		}
	}
	for (auto& session : _sessions)
	{
		if (session.anyChannel == channel)
		{
			session.anyChannel = 0;
		}
	}
	Known(objectId).channel = channel;
}

Session* SessionManager::MatchChannel(std::uintptr_t channel, const std::optional<ScreenKey>& clipKey)
{
	if (channel == 0)
	{
		return nullptr;
	}
	std::optional<std::uint64_t> owner;
	for (const auto& [id, known] : _objects)
	{
		if (known.channel == channel)
		{
			owner = id;
			break;
		}
	}
	if (owner)
	{
		if (auto* session = Find(*owner))
		{
			return session;
		}
	}
	if (clipKey)
	{
		for (auto& [id, known] : _objects)
		{
			if (known.screenKey == clipKey)
			{
				if (auto* session = Find(id))
				{
					BindChannel(id, channel);
					return session;
				}
			}
		}
	}
	Session* any = Find(k_AnyObject);
	if (any == nullptr)
	{
		return nullptr;
	}
	if (any->anyChannel == 0)
	{
		any->anyChannel = channel;
	}
	return any->anyChannel == channel ? any : nullptr;
}

SessionActions SessionManager::OnEvent(const host::HostEvent& event)
{
	SessionActions actions;
	auto* session = FindHelper(event.id);
	if (session == nullptr)
	{
		return actions;
	}
	const auto& type = event.type;
	if (type == "opened")
	{
		session->state = SessionState::Loading;
	}
	else if (type == "ready")
	{
		session->ready = true;
		actions.Add(Apply(*session));
	}
	else if (type == "state")
	{
		const auto state = event.data.value("state", std::string {});
		if (state == "playing")
		{
			session->state = SessionState::Playing;
			// The player can start by itself after `ready` (autoplay). Pause it again if the game is paused.
			if (session->sentPlay == false)
			{
				actions.commands.push_back({.type = host::CommandType::Pause, .id = session->helperId});
			}
		}
		else if (state == "paused")
		{
			session->state = SessionState::Paused;
		}
		else if (state == "buffering")
		{
			session->state = SessionState::Buffering;
		}
		else if (state == "ended")
		{
			session->state = SessionState::Ended;
		}
	}
	else if (type == "error")
	{
		const auto source = event.data.value("source", std::string {});
		session->error = std::format("{}: {}", source, event.data.value("message", std::string {}));
		// A bad command does not stop the video. The other errors do.
		if (source != "command")
		{
			session->state = SessionState::Failed;
		}
	}
	else if (type == "closed")
	{
		const auto it = std::ranges::find(_sessions, event.id, &Session::helperId);
		actions.released.push_back(event.id);
		_sessions.erase(it);
	}
	return actions;
}

SessionActions SessionManager::OnHelperStopped(std::string_view reason)
{
	SessionActions actions;
	for (const auto& session : _sessions)
	{
		actions.released.push_back(session.helperId);
	}
	for (auto& session : _sessions)
	{
		session.state = SessionState::Failed;
		session.error = std::format("the helper stopped: {}", reason);
		session.ready = false;
		session.mapped = false;
	}
	return actions;
}

Session* SessionManager::Find(std::uint64_t objectId)
{
	const auto it = std::ranges::find(_sessions, objectId, &Session::objectId);
	return it == _sessions.end() ? nullptr : &*it;
}

Session* SessionManager::FindHelper(std::int32_t helperId)
{
	const auto it = std::ranges::find(_sessions, helperId, &Session::helperId);
	return it == _sessions.end() ? nullptr : &*it;
}

std::optional<ObjectTransform> SessionManager::TransformOf(std::uint64_t objectId) const
{
	const auto it = _objects.find(objectId);
	return it == _objects.end() ? std::nullopt : it->second.transform;
}

nlohmann::json SessionManager::Status() const
{
	const auto playback = CurrentPlayback();
	nlohmann::json videos = nlohmann::json::array();
	for (const auto& session : _sessions)
	{
		nlohmann::json video {
		    {"objectId", session.objectId},
		    {"id", session.helperId},
		    {"videoId", session.video.id},
		    {"state", SessionStateName(session.state)},
		    {"ready", session.ready},
		    {"mapped", session.mapped},
		    {"framesWritten", session.framesWritten},
		};
		if (!session.error.empty())
		{
			video["error"] = session.error;
		}
		if (const auto it = _objects.find(session.objectId); it != _objects.end())
		{
			video["channelKnown"] = it->second.channel != 0;
			video["screenKey"] = it->second.screenKey.has_value();
			video["transform"] = it->second.transform.has_value();
		}
		else if (session.objectId == k_AnyObject)
		{
			video["channelKnown"] = session.anyChannel != 0;
		}
		videos.push_back(std::move(video));
	}
	return {
	    {"speed", ClockSpeedModeName(_mode)},
	    {"superSpeed", _superSpeed},
	    {"play", playback.play},
	    {"rate", playback.rate},
	    {"muted", playback.muted},
	    {"videos", std::move(videos)},
	    {"knownObjects", _objects.size()},
	};
}

} // namespace ts4_mod::youtube_tv
