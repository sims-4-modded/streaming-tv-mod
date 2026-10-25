/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "SpeedPolicy.h"

#include <charconv>

#include <array>
#include <utility>

namespace ts4_mod::youtube_tv
{

namespace
{

constexpr std::array<std::pair<std::string_view, ClockSpeedMode>, 6> k_Names {{
    {"paused", ClockSpeedMode::Paused},
    {"normal", ClockSpeedMode::Normal},
    {"speed2", ClockSpeedMode::Speed2},
    {"speed3", ClockSpeedMode::Speed3},
    {"startup", ClockSpeedMode::InteractionStartupSpeed},
    {"super", ClockSpeedMode::SuperSpeed3},
}};

} // namespace

std::optional<ClockSpeedMode> ToClockSpeedMode(std::uint32_t value)
{
	if (value > static_cast<std::uint32_t>(ClockSpeedMode::SuperSpeed3))
	{
		return std::nullopt;
	}
	return static_cast<ClockSpeedMode>(value);
}

std::optional<ClockSpeedMode> ParseClockSpeedMode(std::string_view text)
{
	for (const auto& [name, mode] : k_Names)
	{
		if (text == name)
		{
			return mode;
		}
	}
	std::uint32_t value = 0;
	const auto* end = text.data() + text.size();
	const auto [last, error] = std::from_chars(text.data(), end, value);
	if (text.empty() || error != std::errc {} || last != end)
	{
		return std::nullopt;
	}
	return ToClockSpeedMode(value);
}

const char* ClockSpeedModeName(ClockSpeedMode mode)
{
	for (const auto& [name, value] : k_Names)
	{
		if (value == mode)
		{
			return name.data();
		}
	}
	return "unknown";
}

std::uint32_t SpeedLevel(ClockSpeedMode mode)
{
	switch (mode)
	{
	case ClockSpeedMode::Paused:
		return 0;
	case ClockSpeedMode::Normal:
	case ClockSpeedMode::InteractionStartupSpeed:
		return 1;
	case ClockSpeedMode::Speed2:
		return 2;
	case ClockSpeedMode::Speed3:
		return 3;
	case ClockSpeedMode::SuperSpeed3:
		return k_SuperSpeedLevel;
	}
	return 1;
}

Playback SpeedPolicy(ClockSpeedMode mode, bool superSpeed, const SpeedRules& rules)
{
	const auto level = superSpeed && mode != ClockSpeedMode::Paused ? k_SuperSpeedLevel : SpeedLevel(mode);
	Playback playback;
	switch (level)
	{
	case 0:
		playback.play = false;
		break;
	case 1:
		playback.rate = 1.0;
		break;
	case 2:
		playback.rate = rules.speed2Rate;
		break;
	case 3:
		playback.rate = rules.speed3Rate;
		break;
	default:
		playback.play = false;
		break;
	}
	playback.muted = !playback.play || level >= rules.muteLevel;
	return playback;
}

} // namespace ts4_mod::youtube_tv
