/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <string>
#include <string_view>

#include <common/Result.h>
#include <core/AudioSpace.h>
#include <core/FrameWriter.h>
#include <core/Sessions.h>
#include <core/SpeedPolicy.h>
#include <core/Yuv.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::youtube_tv
{

/// The settings of the YouTube TV: the object `youtube_tv` of mods.json of the extender. A missing field keeps its
/// default.
struct YoutubeTvSettings
{
	bool enabled = true;
	/// The helper exe. Empty: `youtube_tv_host.exe` in `%LOCALAPPDATA%\Sims4Mods\youtube_tv`.
	std::string helperPath;
	double volume = 0.8; ///< The volume of the videos, from 0 to 1.
	/// The distance in units of the game at which the volume of a TV falls to one half.
	double audioRange = 3.0;
	std::uint32_t maxSessions = 2;
	std::uint32_t width = 640;  ///< The size of the frames that the helper makes.
	std::uint32_t height = 368; ///< The helper rounds the size up to multiples of 16.
	std::uint32_t frameRate = 30;
	YuvMatrix matrix = YuvMatrix::Bt601;
	YuvRange range = YuvRange::Limited;
	ScaleFilter scaleFilter = ScaleFilter::Bilinear;
	/// What the mod writes into the frames of a TV. Only a test in the game changes it.
	FrameWriteMode frameWrite = FrameWriteMode::Video;
	SpeedRules speed;
	AxisConvention axes;

	[[nodiscard]] bool operator==(const YoutubeTvSettings&) const = default;
};

constexpr double k_MaxAudioRange = 100.0;
constexpr std::uint32_t k_MaxSessions = 8;
constexpr std::uint32_t k_MaxSettingsVideoSize = 1920;
/// A mute level above each speed level: the audio never stops because of the speed.
constexpr std::uint32_t k_NeverMuteLevel = k_SuperSpeedLevel + 1;

/// Read the settings from a JSON object. The result is an error for a field of the wrong type or out of its range.
[[nodiscard]] ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettings(const nlohmann::json& json);

/// Read the settings from a JSON text. Empty text gives the defaults.
[[nodiscard]] ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettingsText(std::string_view text);

/// The settings as a JSON object, with each field.
[[nodiscard]] nlohmann::json YoutubeTvSettingsJson(const YoutubeTvSettings& settings);

/// The settings of the session manager.
[[nodiscard]] SessionSettings SessionSettingsOf(const YoutubeTvSettings& settings);

} // namespace ts4_mod::youtube_tv
