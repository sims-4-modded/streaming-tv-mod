/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "TvRuntime.h"

#include <format>
#include <utility>

namespace ts4_mod::youtube_tv
{

namespace
{

/// The shortest time between two pumps from the callbacks of each frame.
constexpr auto k_PumpInterval = std::chrono::milliseconds(4);
/// The time between two diagnostic lines of the frames, and between two measures of the planes.
constexpr auto k_ReportInterval = std::chrono::seconds(5);
/// The diagnostic lines name only this count of channels, so that the log stays small.
constexpr std::size_t k_MaxSeenChannels = 64;
/// The diagnostic lines of the playlist callbacks name only this count of calls of each callback.
constexpr std::uint32_t k_MaxPlaylistLines = 40;

/// The text of a clip key: the instance, the type and the group.
std::string ClipKeyText(const ScreenKey& key)
{
	return std::format("{:016x}:{:08x}:{:08x}", key.instance, key.type, key.group);
}

/// The text of the rule of a link, for the diagnostic line of the link.
const char* LinkRuleText(SessionManager::LinkRule rule)
{
	switch (rule)
	{
	case SessionManager::LinkRule::Clip:
		return "the same clip";
	case SessionManager::LinkRule::Playlist:
		return "the playlist resource";
	case SessionManager::LinkRule::Time:
		return "the time";
	}
	return "an unknown rule";
}

/// The time of the steady clock in milliseconds, for the link of a channel.
std::chrono::milliseconds NowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

/// The audio ring of one video as the source of a voice. Only the thread of the audio reads it.
class RingSource final: public PcmSource
{
public:
	explicit RingSource(std::shared_ptr<MappedVideo> video)
	    : _video(std::move(video))
	{
	}

	std::size_t Read(std::span<float> out) override { return _video->Audio().Read(out); }
	[[nodiscard]] std::size_t Available() const override { return _video->Audio().AvailableFrames(); }
	void DropOldest(std::size_t keepFrames) override { _video->Audio().DropOldest(keepFrames); }

private:
	std::shared_ptr<MappedVideo> _video;
};

} // namespace

TvRuntime::TvRuntime(YoutubeTvSettings settings, LogFn log, std::unique_ptr<MemoryAccess> memory)
    : _settings(std::move(settings))
    , _log(std::move(log))
    , _memory(std::move(memory))
    , _sessions(SessionSettingsOf(_settings))
{
}

TvRuntime::~TvRuntime()
{
	// Stop the voices before the helper, so that no voice reads shared memory that the client unmaps.
	_audio.reset();
	_client.reset();
}

void TvRuntime::Log(Ts4LogLevel level, const std::string& text) const
{
	if (_log)
	{
		_log(level, text);
	}
}

void TvRuntime::LogOnce(const std::string& key, Ts4LogLevel level, const std::string& text)
{
	if (_logged.insert(key).second)
	{
		Log(level, text);
	}
}

void TvRuntime::SetRendererLayout(const game_layout::RendererLayout& layout)
{
	const std::lock_guard lock(_mutex);
	_renderer = layout;
}

void TvRuntime::SetChannelLayout(const game_layout::ChannelLayout& layout)
{
	const std::lock_guard lock(_mutex);
	_channel = layout;
}

void TvRuntime::SetListenerOffset(std::size_t offset)
{
	const std::lock_guard lock(_mutex);
	_listenerOffset = offset;
}

void TvRuntime::SetVideoSettingsLayout(const game_layout::VideoSettingsLayout& layout)
{
	const std::lock_guard lock(_mutex);
	_videoSettings = layout;
}

void TvRuntime::SetFrameWrite(FrameWriteMode mode)
{
	const std::lock_guard lock(_mutex);
	_settings.frameWrite = mode;
	Log(TS4_LOG_INFO, std::format("frames: the mode is now {}", FrameWriteModeName(mode)));
}

YoutubeTvSettings TvRuntime::Settings() const
{
	const std::lock_guard lock(_mutex);
	return _settings;
}

const Session* TvRuntime::SessionOf(std::uint64_t objectId)
{
	const std::lock_guard lock(_mutex);
	return _sessions.Find(objectId);
}

void TvRuntime::ApplySettings(const YoutubeTvSettings& settings)
{
	const std::lock_guard lock(_mutex);
	_settings = settings;
	if (_audio)
	{
		_audio->SetSettings({.curveDistanceScaler = static_cast<float>(_settings.audioRange)});
	}
	Run(_sessions.SetSettings(SessionSettingsOf(_settings)));
	UpdateAudio();
}

ts4::Result<void> TvRuntime::EnsureHelper()
{
	if (_client && _client->Running())
	{
		return {};
	}
	if (_client)
	{
		// The helper stopped. Use the last events, remove the videos that failed with it, and start a new helper.
		PumpLocked();
		_client.reset();
		Run(_sessions.StopAll());
	}
	std::filesystem::path exe;
	if (_settings.helperPath.empty())
	{
		auto path = HostClient::DefaultExe();
		if (!path)
		{
			return path.GetError();
		}
		exe = *path;
	}
	else
	{
		exe = std::filesystem::path(std::u8string(_settings.helperPath.begin(), _settings.helperPath.end()));
	}
	auto client = HostClient::Start({.exe = exe});
	if (!client)
	{
		return client.GetError();
	}
	_client = std::move(*client);
	Log(TS4_LOG_INFO, std::format("the helper started (process {})", _client->ProcessId()));
	return {};
}

void TvRuntime::EnsureAudio()
{
	if (_audio || _audioFailed)
	{
		return;
	}
	auto audio = AudioEngine::Start({.curveDistanceScaler = static_cast<float>(_settings.audioRange)});
	if (!audio)
	{
		_audioFailed = true;
		Log(TS4_LOG_WARN, "the videos play with no audio: " + audio.GetError().message);
		return;
	}
	_audio = std::move(*audio);
	if (_listener)
	{
		_audio->SetListener(*_listener);
	}
	Log(TS4_LOG_INFO, std::format("the 3D audio started with {} output channels", _audio->OutputChannels()));
}

ts4::Result<void> TvRuntime::Play(std::uint64_t objectId, std::string_view url, std::optional<ObjectTransform> transform)
{
	const std::lock_guard lock(_mutex);
	if (!_settings.enabled)
	{
		return ts4::MakeError("the mod is off in its settings");
	}
	if (auto started = EnsureHelper(); !started)
	{
		return ts4::MakeError("the helper cannot start: {}", started.GetError().message);
	}
	auto actions = _sessions.Play(objectId, url, transform);
	if (!actions)
	{
		return actions.GetError();
	}
	Run(std::move(*actions));
	EnsureAudio();
	Log(TS4_LOG_INFO, std::format("play {} on the object {:#x}", _sessions.Find(objectId)->video.id, objectId));
	return {};
}

bool TvRuntime::Stop(std::uint64_t objectId)
{
	const std::lock_guard lock(_mutex);
	auto actions = _sessions.Stop(objectId);
	if (!actions)
	{
		return false;
	}
	Run(std::move(*actions));
	return true;
}

void TvRuntime::StopAll()
{
	const std::lock_guard lock(_mutex);
	Run(_sessions.StopAll());
}

void TvRuntime::SetSpeed(ClockSpeedMode mode, bool superSpeed, double scale)
{
	const std::lock_guard lock(_mutex);
	_speedScale = scale;
	Run(_sessions.SetSpeed(mode, superSpeed));
	UpdateAudio();
}

void TvRuntime::SetTransform(std::uint64_t objectId, const ObjectTransform& transform)
{
	const std::lock_guard lock(_mutex);
	_sessions.SetTransform(objectId, transform);
	UpdateAudio();
}

void TvRuntime::SetScreenKey(std::uint64_t objectId, const ScreenKey& key)
{
	const std::lock_guard lock(_mutex);
	_sessions.SetScreenKey(objectId, key);
}

ts4::Result<void> TvRuntime::SetPlaylistClips(const ScreenKey& playlist, std::vector<ScreenKey> clips)
{
	const std::lock_guard lock(_mutex);
	return _sessions.SetPlaylistClips(playlist, std::move(clips));
}

void TvRuntime::Run(SessionActions&& actions)
{
	for (const auto& command : actions.commands)
	{
		if (_client && !_client->Send(command))
		{
			LogOnce("send", TS4_LOG_WARN, "the helper does not take commands");
		}
	}
	for (const auto id : actions.released)
	{
		if (_audio)
		{
			_audio->RemoveVoice(id);
		}
		if (_client)
		{
			_client->Release(id);
		}
	}
}

void TvRuntime::OnEvent(const host::HostEvent& event)
{
	if (event.type == "exit")
	{
		Log(TS4_LOG_WARN, "the helper stopped");
		Run(_sessions.OnHelperStopped("the pipe of the events closed"));
		return;
	}
	if (event.type == "hello")
	{
		Log(TS4_LOG_INFO, "the helper says hello: " + event.data.dump());
		return;
	}
	auto* session = _sessions.FindHelper(event.id);
	if (session == nullptr)
	{
		return;
	}
	if (event.type == "mapError")
	{
		session->state = SessionState::Failed;
		session->error = "the shared memory: " + event.data.value("message", std::string {});
		Log(TS4_LOG_ERROR, std::format("video {}: {}", event.id, session->error));
		return;
	}
	if (event.type == "error")
	{
		Log(TS4_LOG_WARN, std::format("video {}: {}", event.id, event.data.dump()));
	}
	if (event.type == "opened" && _client)
	{
		if (auto video = _client->Video(event.id))
		{
			session->mapped = true;
			if (_audio)
			{
				if (auto added = _audio->AddVoice(event.id, std::make_shared<RingSource>(video)); !added)
				{
					Log(TS4_LOG_WARN, std::format("video {} has no audio: {}", event.id, added.GetError().message));
				}
			}
		}
	}
	Run(_sessions.OnEvent(event));
}

void TvRuntime::Pump()
{
	const std::lock_guard lock(_mutex);
	PumpLocked();
}

void TvRuntime::PumpLocked()
{
	_lastPump = std::chrono::steady_clock::now();
	if (!_client)
	{
		return;
	}
	for (const auto& event : _client->TakeEvents())
	{
		OnEvent(event);
	}
	UpdateAudio();
}

void TvRuntime::UpdateAudio()
{
	if (!_audio)
	{
		return;
	}
	const auto playback = _sessions.CurrentPlayback();
	for (const auto& session : _sessions.Sessions())
	{
		std::optional<AudioPose> emitter;
		if (const auto transform = _sessions.TransformOf(session.objectId))
		{
			emitter = EmitterPose(*transform, _settings.axes);
		}
		_audio->SetVoice(session.helperId, emitter, VoiceVolume(_settings.volume, playback.muted, session.state));
	}
	_audio->Update();
}

std::shared_ptr<MappedVideo> TvRuntime::VideoOf(std::uint64_t objectId)
{
	const std::lock_guard lock(_mutex);
	const auto* session = _sessions.Find(objectId);
	if (session == nullptr || !_client)
	{
		return nullptr;
	}
	return _client->Video(session->helperId);
}

void TvRuntime::SetListenerPose(const AudioPose& pose)
{
	const std::lock_guard lock(_mutex);
	SetListenerPoseLocked(pose);
}

void TvRuntime::SetListenerPoseLocked(const AudioPose& pose)
{
	_listener = pose;
	if (_audio)
	{
		_audio->SetListener(pose);
	}
}

void TvRuntime::OnPlaylistMessage(std::uintptr_t message)
{
	const std::lock_guard lock(_mutex);
	if (!_channel)
	{
		return;
	}
	const auto objectId = ReadMessageObjectId(*_memory, message, *_channel);
	auto keys = ReadMessageClipKeys(*_memory, message, *_channel);
	if (_playlistMessageLines < k_MaxPlaylistLines)
	{
		++_playlistMessageLines;
		std::string keysText;
		if (!keys)
		{
			keysText = "clip keys not known: " + keys.GetError().message;
		}
		else
		{
			keysText = std::format("{} clip keys", keys->size());
			for (const auto& key : *keys)
			{
				keysText += " " + ClipKeyText(key);
			}
		}
		Log(TS4_LOG_INFO, std::format("frames: playlist message {} for the object {}: {}", _playlistMessageLines,
		                              objectId ? std::format("{:#x}", *objectId) : std::string("unknown"), keysText));
	}
	if (objectId && keys)
	{
		_sessions.NotePlaylist(*objectId, std::move(*keys), NowMs());
	}
}

void TvRuntime::OnRenderFrame(std::uintptr_t renderer, std::uintptr_t frame)
{
	// Under the lock: find the video of this channel. The frame copy then occurs without the lock, so that a call of
	// the bridge on the thread of the game Python never waits for a copy.
	std::shared_ptr<MappedVideo> video;
	std::int32_t helperId = 0;
	game_layout::RendererLayout layout;
	ScaleFilter filter {};
	YuvRange range {};
	FrameWriteMode mode {};
	bool measure = false;
	{
		const std::lock_guard lock(_mutex);
		++_renderCalls;
		if (std::chrono::steady_clock::now() - _lastPump >= k_PumpInterval)
		{
			PumpLocked();
		}
		if (!_renderer)
		{
			return;
		}
		const auto channel = ReadRendererChannel(*_memory, renderer, *_renderer);
		std::optional<ScreenKey> clipKey;
		if (_channel)
		{
			clipKey = ReadChannelClipKey(*_memory, channel, *_channel);
		}
		if (clipKey)
		{
			if (const auto linked = _sessions.LinkChannelByClip(channel, *clipKey, NowMs()))
			{
				if (_linkLines < k_MaxPlaylistLines || _sessions.Find(linked->objectId) != nullptr)
				{
					++_linkLines;
					const char* rule = LinkRuleText(linked->rule);
					Log(TS4_LOG_INFO,
					    std::format("frames: the playlist of the object {:#x} links the channel {:#x} (clip {}, by {})",
					                linked->objectId, channel, ClipKeyText(*clipKey), rule));
				}
			}
		}
		if (channel != 0 && _seenChannels.size() < k_MaxSeenChannels && _seenChannels.insert(channel).second)
		{
			const auto size = ReadRendererSize(*_memory, renderer, *_renderer);
			const auto target = ReadGameFrame(*_memory, frame, *_renderer);
			const auto clipText = clipKey ? ClipKeyText(*clipKey) : std::string("none");
			const auto sizeText = size ? std::format("{}x{}", (*size)[0], (*size)[1]) : std::string("unknown");
			const auto planeText =
			    target ? std::format("{}x{}", target->planes[0].width, target->planes[0].height) : std::string("not correct");
			const char* flipText = target ? (target->flip ? "on" : "off") : "unknown";
			Log(TS4_LOG_INFO,
			    std::format("frames: the first frame of the channel {:#x}: clip {}, target {}, planes {}, flip {}, {}", channel,
			                clipText, sizeText, planeText, flipText, VideoSettingsTextLocked()));
		}
		if (!_sessions.Sessions().empty())
		{
			ReportFramesLocked();
		}
		if (!_client || _sessions.Sessions().empty())
		{
			return;
		}
		auto* session = _sessions.MatchChannel(channel, clipKey);
		if (session == nullptr || session->state == SessionState::Failed)
		{
			return;
		}
		++_matchedCalls;
		mode = _settings.frameWrite;
		if (mode == FrameWriteMode::Off)
		{
			return;
		}
		video = _client->Video(session->helperId);
		if (!video && mode == FrameWriteMode::Video)
		{
			return;
		}
		helperId = session->helperId;
		layout = *_renderer;
		filter = _settings.scaleFilter;
		range = _settings.range;
		const auto now = std::chrono::steady_clock::now();
		if (now - _lastMeasure >= k_ReportInterval)
		{
			_lastMeasure = now;
			measure = true;
		}
	}
	// Only this thread reads the frames of the helper, so the triple buffer needs no lock.
	std::uint64_t frontFrame = 0;
	if (mode == FrameWriteMode::Video)
	{
		auto& frames = video->Frames();
		frames.TakeNewest();
		frontFrame = frames.FrontFrameNumber();
		if (frontFrame == 0)
		{
			const std::lock_guard lock(_mutex);
			_frontFrame = 0;
			return;
		}
	}
	const auto target = ReadGameFrame(*_memory, frame, layout);
	if (!target)
	{
		const std::lock_guard lock(_mutex);
		LogOnce(std::format("frame {}", helperId), TS4_LOG_WARN,
		        std::format("video {}: the frame of the game is not correct; the mod does not write it", helperId));
		return;
	}
	std::array<double, 3> before {};
	if (measure)
	{
		before = PlaneMeans(*target);
	}
	ts4::Result<FrameWrite> written = FrameWrite::Copy;
	if (mode == FrameWriteMode::Bars)
	{
		if (auto bars = WriteColourBars(*target, range); !bars)
		{
			written = bars.GetError();
		}
	}
	else
	{
		auto& frames = video->Frames();
		written = WriteI420Frame(frames.FrontPlanes(), *target, filter, frames.Range());
	}
	std::array<double, 3> after {};
	if (measure && written)
	{
		after = PlaneMeans(*target);
	}
	const std::lock_guard lock(_mutex);
	_frontFrame = frontFrame;
	if (measure && written)
	{
		_meansBefore = before;
		_meansAfter = after;
		_measured = true;
	}
	if (!written)
	{
		LogOnce(std::format("write {}", helperId), TS4_LOG_WARN,
		        std::format("video {}: {}", helperId, written.GetError().message));
		return;
	}
	// The video can stop while the lock is free. Then the session is not there, and the count does not change.
	auto* session = _sessions.FindHelper(helperId);
	if (session == nullptr)
	{
		return;
	}
	if (session->framesWritten == 0)
	{
		const auto sourceText = mode == FrameWriteMode::Video
		                            ? std::format("{}x{}", video->Frames().Width(), video->Frames().Height())
		                            : std::string("bars");
		Log(TS4_LOG_INFO, std::format("video {}: the first frame on the screen ({} into {}x{}, {}, mode {})", helperId,
		                              sourceText, target->planes[0].width, target->planes[0].height,
		                              *written == FrameWrite::Copy ? "copy" : "letterbox", FrameWriteModeName(mode)));
	}
	++session->framesWritten;
	++_framesWritten;
}

void TvRuntime::ReportFramesLocked()
{
	const auto now = std::chrono::steady_clock::now();
	if (now - _lastReport < k_ReportInterval)
	{
		return;
	}
	_lastReport = now;
	const auto means = [](const std::array<double, 3>& values) {
		return std::format("Y {:.1f} U {:.1f} V {:.1f}", values[0], values[1], values[2]);
	};
	const auto beforeText = _measured ? means(_meansBefore) : std::string("not measured");
	const auto afterText = _measured ? means(_meansAfter) : std::string("not measured");
	Log(TS4_LOG_INFO, std::format("frames: mode {}, renderCalls {}, channels {}, calls for a video {}, framesWritten {}, "
	                              "helper frame {}, before the write {}, after the write {}, {}",
	                              FrameWriteModeName(_settings.frameWrite), _renderCalls, _seenChannels.size(), _matchedCalls,
	                              _framesWritten, _frontFrame, beforeText, afterText, VideoSettingsTextLocked()));
}

std::string TvRuntime::VideoSettingsTextLocked() const
{
	if (!_videoSettings)
	{
		return "video settings unknown";
	}
	const auto manager = ReadValue<std::uintptr_t>(*_memory, _videoSettings->managerGlobal);
	if (!manager || *manager == 0)
	{
		return "no video manager";
	}
	const auto flags = ReadValue<std::array<std::uint8_t, 2>>(*_memory, *manager + _videoSettings->settingsOffset);
	if (!flags)
	{
		return "video settings not readable";
	}
	return std::format("video settings draw {} copy {}", (*flags)[0], (*flags)[1]);
}

void TvRuntime::OnListener(std::uintptr_t state)
{
	const std::lock_guard lock(_mutex);
	if (std::chrono::steady_clock::now() - _lastPump >= k_PumpInterval)
	{
		PumpLocked();
	}
	if (!_listenerOffset)
	{
		return;
	}
	const auto matrix = ReadListenerMatrix(*_memory, state, *_listenerOffset);
	if (!matrix)
	{
		return;
	}
	const auto pose = ListenerPose(*matrix, _settings.axes);
	if (!pose)
	{
		LogOnce("listener", TS4_LOG_WARN, "the camera matrix of the listener is not a rotation and a position");
		return;
	}
	SetListenerPoseLocked(*pose);
}

nlohmann::json TvRuntime::Status()
{
	const std::lock_guard lock(_mutex);
	PumpLocked();
	auto status = _sessions.Status();
	status["enabled"] = _settings.enabled;
	status["speedScale"] = _speedScale;
	status["helper"] = _client ? (_client->Running() ? "running" : "stopped") : "not started";
	status["hooks"] = {
	    {"renderer", _renderer.has_value()}, {"channel", _channel.has_value()}, {"listener", _listenerOffset.has_value()}};
	status["renderCalls"] = _renderCalls;
	status["framesWritten"] = _framesWritten;
	status["frameWrite"] = FrameWriteModeName(_settings.frameWrite);
	status["channelsSeen"] = _seenChannels.size();
	status["callsForVideo"] = _matchedCalls;
	status["helperFrame"] = _frontFrame;
	status["videoSettings"] = VideoSettingsTextLocked();
	status["listenerKnown"] = _listener.has_value();
	status["audio"] = _audio ? _audio->Status() : nlohmann::json(_audioFailed ? "failed" : "not started");
	return status;
}

} // namespace ts4_mod::youtube_tv
