/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <string_view>

#include <common/Result.h>
#include <nlohmann/json.hpp>

namespace ts4_mod::youtube_tv
{

/// The settings of the YouTube TV: the file `youtube_tv.json` in the settings folder of the extender. A missing
/// field keeps its default.
struct YoutubeTvSettings
{
	bool enabled = true;
	double strength = 1.0; ///< An example of a number with a range, from 0 to 10.

	[[nodiscard]] bool operator==(const YoutubeTvSettings&) const = default;
};

constexpr double k_MinStrength = 0.0;
constexpr double k_MaxStrength = 10.0;

/// Read the settings from a JSON object. The result is an error for a field of the wrong type or out of its range.
[[nodiscard]] ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettings(const nlohmann::json& json);

/// Read the settings from the text of the settings file. Empty text gives the defaults.
[[nodiscard]] ts4::Result<YoutubeTvSettings> ParseYoutubeTvSettingsText(std::string_view text);

/// The settings as a JSON object, with each field.
[[nodiscard]] nlohmann::json YoutubeTvSettingsJson(const YoutubeTvSettings& settings);

} // namespace ts4_mod::youtube_tv
