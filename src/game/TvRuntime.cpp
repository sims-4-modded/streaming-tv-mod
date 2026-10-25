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

void TvRuntime::ApplySettings(const YoutubeTvSettings& settings)
{
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
		Pump();
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
	Run(_sessions.StopAll());
}

void TvRuntime::SetSpeed(ClockSpeedMode mode, bool superSpeed, double scale)
{
	_speedScale = scale;
	Run(_sessions.SetSpeed(mode, superSpeed));
	UpdateAudio();
}

void TvRuntime::SetTransform(std::uint64_t objectId, const ObjectTransform& transform)
{
	_sessions.SetTransform(objectId, transform);
	UpdateAudio();
}

void TvRuntime::SetScreenKey(std::uint64_t objectId, const ScreenKey& key)
{
	_sessions.SetScreenKey(objectId, key);
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
	const auto* session = _sessions.Find(objectId);
	if (session == nullptr || !_client)
	{
		return nullptr;
	}
	return _client->Video(session->helperId);
}

void TvRuntime::SetListenerPose(const AudioPose& pose)
{
	_listener = pose;
	if (_audio)
	{
		_audio->SetListener(pose);
	}
}

void TvRuntime::OnPlaylistMessage(std::uintptr_t message)
{
	_pendingObject.reset();
	if (_channel)
	{
		_pendingObject = ReadMessageObjectId(*_memory, message, *_channel);
	}
}

void TvRuntime::OnPlaylistMessageDone()
{
	_pendingObject.reset();
}

void TvRuntime::OnSetPlaylist(std::uintptr_t component)
{
	if (!_channel || !_pendingObject)
	{
		return;
	}
	const auto channel = ReadComponentChannel(*_memory, component, *_channel);
	if (channel != 0)
	{
		_sessions.BindChannel(*_pendingObject, channel);
	}
	_pendingObject.reset();
}

void TvRuntime::OnRenderFrame(std::uintptr_t renderer, std::uintptr_t frame)
{
	++_renderCalls;
	if (std::chrono::steady_clock::now() - _lastPump >= k_PumpInterval)
	{
		Pump();
	}
	if (!_renderer || !_client || _sessions.Sessions().empty())
	{
		return;
	}
	const auto channel = ReadRendererChannel(*_memory, renderer, *_renderer);
	std::optional<ScreenKey> clipKey;
	if (_channel)
	{
		clipKey = ReadChannelClipKey(*_memory, channel, *_channel);
	}
	auto* session = _sessions.MatchChannel(channel, clipKey);
	if (session == nullptr || session->state == SessionState::Failed)
	{
		return;
	}
	auto video = _client->Video(session->helperId);
	if (!video)
	{
		return;
	}
	auto& frames = video->Frames();
	frames.TakeNewest();
	if (frames.FrontFrameNumber() == 0)
	{
		return;
	}
	const auto target = ReadGameFrame(*_memory, frame, *_renderer);
	if (!target)
	{
		LogOnce(std::format("frame {}", session->helperId), TS4_LOG_WARN,
		        std::format("video {}: the frame of the game is not correct; the mod does not write it", session->helperId));
		return;
	}
	const auto written = WriteI420Frame(frames.FrontPlanes(), *target, _settings.scaleFilter, frames.Range());
	if (!written)
	{
		LogOnce(std::format("write {}", session->helperId), TS4_LOG_WARN,
		        std::format("video {}: {}", session->helperId, written.GetError().message));
		return;
	}
	if (session->framesWritten == 0)
	{
		Log(TS4_LOG_INFO, std::format("video {}: the first frame on the screen ({}x{} into {}x{}, {})", session->helperId,
		                              frames.Width(), frames.Height(), target->planes[0].width, target->planes[0].height,
		                              *written == FrameWrite::Copy ? "copy" : "letterbox"));
	}
	++session->framesWritten;
	++_framesWritten;
}

void TvRuntime::OnListener(std::uintptr_t state)
{
	if (std::chrono::steady_clock::now() - _lastPump >= k_PumpInterval)
	{
		Pump();
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
	SetListenerPose(*pose);
}

nlohmann::json TvRuntime::Status()
{
	Pump();
	auto status = _sessions.Status();
	status["enabled"] = _settings.enabled;
	status["speedScale"] = _speedScale;
	status["helper"] = _client ? (_client->Running() ? "running" : "stopped") : "not started";
	status["hooks"] = {
	    {"renderer", _renderer.has_value()}, {"channel", _channel.has_value()}, {"listener", _listenerOffset.has_value()}};
	status["renderCalls"] = _renderCalls;
	status["framesWritten"] = _framesWritten;
	status["listenerKnown"] = _listener.has_value();
	status["audio"] = _audio ? _audio->Status() : nlohmann::json(_audioFailed ? "failed" : "not started");
	return status;
}

} // namespace ts4_mod::youtube_tv
