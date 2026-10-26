/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
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
/// game. The probe tool calls it with fake objects of the game.
///
/// Two threads of the game call it. The client main thread (the tick of the extender) runs the hook callbacks and the
/// queries; only these read or write the memory of the game. The thread of the game Python runs the bridge of the
/// script mod; the bridge changes only the sessions and the settings. Each public function takes one lock. The lock
/// covers only short work on the state of the runtime: commands to the helper go to the queue of its writer thread,
/// and the copy of a frame into the planes of the game occurs after the lock is released.
class TvRuntime
{
public:
	using LogFn = std::function<void(Ts4LogLevel level, const std::string& text)>;

	TvRuntime(YoutubeTvSettings settings, LogFn log, std::unique_ptr<MemoryAccess> memory);
	~TvRuntime();
	TvRuntime(const TvRuntime&) = delete;
	TvRuntime& operator=(const TvRuntime&) = delete;

	/// Give the offsets that the mod read from the code of the game. A part with no offsets stays off.
	void SetRendererLayout(const game_layout::RendererLayout& layout);
	void SetChannelLayout(const game_layout::ChannelLayout& layout);
	void SetListenerOffset(std::size_t offset);
	/// Give the place of the video settings of the game. The diagnostic lines then show the two flags of the copy and
	/// the draw.
	void SetVideoSettingsLayout(const game_layout::VideoSettingsLayout& layout);

	/// Use new settings. The size, the frame rate and the matrix apply to the next videos.
	void ApplySettings(const YoutubeTvSettings& settings);
	[[nodiscard]] YoutubeTvSettings Settings() const;
	/// Change what the mod writes into the frames, while the game runs. Only a test in the game uses it.
	void SetFrameWrite(FrameWriteMode mode);

	// The calls of the script mod and of the inspector.

	[[nodiscard]] ts4::Result<void> Play(std::uint64_t objectId, std::string_view url,
	                                     std::optional<ObjectTransform> transform = std::nullopt);
	bool Stop(std::uint64_t objectId);
	void StopAll();
	void SetSpeed(ClockSpeedMode mode, bool superSpeed, double scale = 1.0);
	void SetTransform(std::uint64_t objectId, const ObjectTransform& transform);
	void SetScreenKey(std::uint64_t objectId, const ScreenKey& key);
	/// Keep the clip keys of a playlist resource (`SessionManager::SetPlaylistClips`).
	[[nodiscard]] ts4::Result<void> SetPlaylistClips(const ScreenKey& playlist, std::vector<ScreenKey> clips);
	[[nodiscard]] nlohmann::json Status();

	// The hook callbacks.

	/// Before the game applies a list of clips to an object: keep the clip keys of the list for the object of the
	/// message.
	void OnPlaylistMessage(std::uintptr_t message);
	/// Before the game copies a frame into the textures of the screen: link the channel to the object that the game
	/// gave its clip to, and write the newest frame of the video of this channel into the planes.
	void OnRenderFrame(std::uintptr_t renderer, std::uintptr_t frame);
	/// After the game put its audio listener at the camera.
	void OnListener(std::uintptr_t state);

	/// Use the events of the helper and update the audio. The callbacks call it; a caller can also call it.
	void Pump();

	/// The listener for the probe, which has no camera.
	void SetListenerPose(const AudioPose& pose);

	/// The shared memory of the video of a TV, or null. The probe uses it.
	[[nodiscard]] std::shared_ptr<MappedVideo> VideoOf(std::uint64_t objectId);
	/// The session of a TV, or null. Only the probe uses it, on one thread: the pointer is not safe for two threads.
	[[nodiscard]] const Session* SessionOf(std::uint64_t objectId);

	/// The audio engine, or null. The probe uses it.
	[[nodiscard]] AudioEngine* Audio() { return _audio.get(); }

private:
	// The functions below run with the lock held.

	void PumpLocked();
	void SetListenerPoseLocked(const AudioPose& pose);
	void Log(Ts4LogLevel level, const std::string& text) const;
	/// Write a line only the first time for this key.
	void LogOnce(const std::string& key, Ts4LogLevel level, const std::string& text);
	[[nodiscard]] ts4::Result<void> EnsureHelper();
	void EnsureAudio();
	void Run(SessionActions&& actions);
	void OnEvent(const host::HostEvent& event);
	void UpdateAudio();
	/// Write the diagnostic line of the frames, if the last line is old enough.
	void ReportFramesLocked();
	/// The two flags of the video settings of the game (byte 0 draw, byte 1 copy), or text that tells why not.
	[[nodiscard]] std::string VideoSettingsTextLocked() const;

	/// The lock of the state below. The log function and the memory access need no lock.
	mutable std::mutex _mutex;
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
	std::optional<game_layout::VideoSettingsLayout> _videoSettings;

	std::optional<AudioPose> _listener;
	double _speedScale = 1.0;
	std::set<std::string> _logged;
	std::chrono::steady_clock::time_point _lastPump {};
	std::uint64_t _framesWritten = 0;
	std::uint64_t _renderCalls = 0;

	// The diagnostics of the frames.
	std::set<std::uintptr_t> _seenChannels;
	std::uint64_t _matchedCalls = 0;
	std::uint32_t _playlistMessageLines = 0;
	std::uint32_t _linkLines = 0;
	std::uint64_t _frontFrame = 0;
	std::array<double, 3> _meansBefore {};
	std::array<double, 3> _meansAfter {};
	bool _measured = false;
	std::chrono::steady_clock::time_point _lastReport {};
	std::chrono::steady_clock::time_point _lastMeasure {};
};

} // namespace ts4_mod::youtube_tv
