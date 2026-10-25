/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

#include <common/Result.h>
#include <core/AudioSpace.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::youtube_tv
{

/// Stereo float PCM at 48000 frames each second, for one voice.
class PcmSource
{
public:
	virtual ~PcmSource() = default;

	/// Read interleaved frames into `out`. Return the number of frames that the source read.
	virtual std::size_t Read(std::span<float> out) = 0;
	/// The frames that the source can give now.
	[[nodiscard]] virtual std::size_t Available() const = 0;
	/// Remove old frames, so that a maximum of `keepFrames` stay.
	virtual void DropOldest(std::size_t keepFrames) = 0;
};

/// The 3D audio of the videos, with XAudio2 and X3DAudio of Windows. The engine loads `xaudio2_9.dll` from the
/// system folder; it brings no DLL into the game folder. One source voice plays each video. A thread of the engine
/// feeds the voices from their sources. The main thread gives the listener and the emitters, and calls `Update`.
class AudioEngine
{
public:
	/// The distance at which the volume of a TV falls to one half, in units of the game.
	struct Settings
	{
		float curveDistanceScaler = 3.0F;
	};

	/// Start XAudio2 on the default output device. The result is an error if the system has no XAudio2 2.9 or no
	/// output device.
	[[nodiscard]] static ts4::Result<std::unique_ptr<AudioEngine>> Start(Settings settings);

	~AudioEngine();
	AudioEngine(const AudioEngine&) = delete;
	AudioEngine& operator=(const AudioEngine&) = delete;

	/// Make a voice for a video. A voice with the same id is replaced.
	[[nodiscard]] ts4::Result<void> AddVoice(std::int32_t id, std::shared_ptr<PcmSource> source);
	void RemoveVoice(std::int32_t id);
	void RemoveAll();
	[[nodiscard]] bool HasVoice(std::int32_t id) const;

	void SetListener(const AudioPose& listener);
	/// Give the position and the volume of a voice. A voice with no position plays with no 3D effect.
	void SetVoice(std::int32_t id, const std::optional<AudioPose>& emitter, float volume);
	void SetSettings(Settings settings);

	/// Compute the 3D audio of each voice and give it to XAudio2. The main thread calls it after a change of the
	/// listener or of a voice.
	void Update();

	[[nodiscard]] std::uint32_t OutputChannels() const { return _outputChannels; }
	[[nodiscard]] nlohmann::json Status() const;

private:
	struct Library;
	struct Voice;

	AudioEngine() = default;

	void Feed(std::stop_token stop);

	std::unique_ptr<Library> _library;
	std::uint32_t _outputChannels = 2;
	Settings _settings;

	mutable std::mutex _mutex;
	std::map<std::int32_t, std::unique_ptr<Voice>> _voices;
	AudioPose _listener;
	std::jthread _feeder;
};

} // namespace ts4_mod::youtube_tv
