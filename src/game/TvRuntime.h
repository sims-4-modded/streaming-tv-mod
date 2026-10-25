/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <common/Result.h>
#include <core/AudioSpace.h>
#include <core/GameObjects.h>
#include <core/Sessions.h>
#include <core/VideoLayout.h>
#include <core/YoutubeTvSettings.h>
#include <mod_api/Ts4ModApi.h>
#include <nlohmann/json.hpp>

#include "game/AudioEngine.h"
#include "game/HostClient.h"

namespace ts4_mod::youtube_tv
{

/// The YouTube TV without the extender: the sessions, the helper, the 3D audio and the writes into the frames of the
/// game. The hook callbacks, the queries and the bridge of the script mod call it, all on the main thread. The
/// probe tool calls it with fake objects of the game.
class TvRuntime
{
public:
	using LogFn = std::function<void(Ts4LogLevel level, const std::string& text)>;

	TvRuntime(YoutubeTvSettings settings, LogFn log, std::unique_ptr<MemoryAccess> memory);
	~TvRuntime();
	TvRuntime(const TvRuntime&) = delete;
	TvRuntime& operator=(const TvRuntime&) = delete;

	/// Give the offsets that the mod read from the code of the game. A part with no offsets stays off.
	void SetRendererLayout(const game_layout::RendererLayout& layout) { _renderer = layout; }
	void SetChannelLayout(const game_layout::ChannelLayout& layout) { _channel = layout; }
	void SetListenerOffset(std::size_t offset) { _listenerOffset = offset; }

	/// Use new settings. The size, the frame rate and the matrix apply to the next videos.
	void ApplySettings(const YoutubeTvSettings& settings);
	[[nodiscard]] const YoutubeTvSettings& Settings() const { return _settings; }

	// The calls of the script mod and of the inspector.

	[[nodiscard]] ts4::Result<void> Play(std::uint64_t objectId, std::string_view url,
	                                     std::optional<ObjectTransform> transform = std::nullopt);
	bool Stop(std::uint64_t objectId);
	void StopAll();
	void SetSpeed(ClockSpeedMode mode, bool superSpeed, double scale = 1.0);
	void SetTransform(std::uint64_t objectId, const ObjectTransform& transform);
	void SetScreenKey(std::uint64_t objectId, const ScreenKey& key);
	[[nodiscard]] nlohmann::json Status();

	// The hook callbacks.

	/// Before the game applies a list of clips to an object: keep the object id of the message.
	void OnPlaylistMessage(std::uintptr_t message);
	/// After the game applied the message.
	void OnPlaylistMessageDone();
	/// Before the video component gets its list of clips: link the object of the message to the channel.
	void OnSetPlaylist(std::uintptr_t component);
	/// Before the game copies a frame into the textures of the screen: write the newest frame of the video of this
	/// channel into the planes.
	void OnRenderFrame(std::uintptr_t renderer, std::uintptr_t frame);
	/// After the game put its audio listener at the camera.
	void OnListener(std::uintptr_t state);

	/// Use the events of the helper and update the audio. The callbacks call it; a caller can also call it.
	void Pump();

	/// The listener for the probe, which has no camera.
	void SetListenerPose(const AudioPose& pose);

	/// The audio engine, or null. The probe uses it.
	[[nodiscard]] AudioEngine* Audio() { return _audio.get(); }

private:
	void Log(Ts4LogLevel level, const std::string& text) const;
	/// Write a line only the first time for this key.
	void LogOnce(const std::string& key, Ts4LogLevel level, const std::string& text);
	[[nodiscard]] ts4::Result<void> EnsureHelper();
	void EnsureAudio();
	void Run(SessionActions&& actions);
	void OnEvent(const host::HostEvent& event);
	void UpdateAudio();

	YoutubeTvSettings _settings;
	LogFn _log;
	std::unique_ptr<MemoryAccess> _memory;
	SessionManager _sessions;
	std::unique_ptr<HostClient> _client;
	std::unique_ptr<AudioEngine> _audio;
	bool _audioFailed = false;

	std::optional<game_layout::RendererLayout> _renderer;
	std::optional<game_layout::ChannelLayout> _channel;
	std::optional<std::size_t> _listenerOffset;

	std::optional<std::uint64_t> _pendingObject;
	std::optional<AudioPose> _listener;
	double _speedScale = 1.0;
	std::set<std::string> _logged;
	std::chrono::steady_clock::time_point _lastPump {};
	std::uint64_t _framesWritten = 0;
	std::uint64_t _renderCalls = 0;
};

} // namespace ts4_mod::youtube_tv
