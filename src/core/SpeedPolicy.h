/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <string_view>

/// What the video of a TV does at each speed of the clock of the game.
namespace ts4_mod::youtube_tv
{

/// The values of `clock.ClockSpeedMode` of the game Python.
enum class ClockSpeedMode : std::uint32_t
{
	Paused = 0,
	Normal = 1,
	Speed2 = 2,
	Speed3 = 3,
	InteractionStartupSpeed = 4,
	SuperSpeed3 = 5,
};

/// Read a value of `clock.ClockSpeedMode`. The result is empty for an unknown value.
[[nodiscard]] std::optional<ClockSpeedMode> ToClockSpeedMode(std::uint32_t value);
/// Read a name, for example "paused", "normal", "speed2", "speed3", "startup" or "super", or a number.
[[nodiscard]] std::optional<ClockSpeedMode> ParseClockSpeedMode(std::string_view text);
[[nodiscard]] const char* ClockSpeedModeName(ClockSpeedMode mode);

/// The order of the speeds, from 0 (paused) to 4 (super speed). The interaction start-up speed has the order of the
/// normal speed, because the clock runs at the normal speed then.
[[nodiscard]] std::uint32_t SpeedLevel(ClockSpeedMode mode);

constexpr std::uint32_t k_SuperSpeedLevel = 4;

/// The settings of the speed policy.
struct SpeedRules
{
	double speed2Rate = 2.0; ///< The playback rate at the speed 2.
	double speed3Rate = 2.0; ///< The playback rate at the speed 3. The player has no rate above 2.
	/// The audio of the video stops at this speed level and at higher levels (see `SpeedLevel`). The game does the
	/// same with the audio of its own video at the `mute_speed` of the TV.
	std::uint32_t muteLevel = k_SuperSpeedLevel;

	[[nodiscard]] bool operator==(const SpeedRules&) const = default;
};

/// What the player does at one speed of the clock.
struct Playback
{
	bool play = true;   ///< False: the player pauses, and the screen keeps the last frame.
	double rate = 1.0;  ///< The playback rate when the player plays.
	bool muted = false; ///< The audio of the video is silent.

	[[nodiscard]] bool operator==(const Playback&) const = default;
};

/// The policy:
/// - Paused: the player pauses, and the audio is silent.
/// - Normal and the interaction start-up speed: the rate 1.
/// - Speed 2 and speed 3: the rates of the rules (2 by default, the maximum of the player).
/// - Super speed: the player pauses, and the screen keeps the last frame under the distortion effect of the game.
///   The clock of the game is then many times faster than the rate 2 of the player. Thus a video at the rate 2 does
///   not follow the game, and its audio would be wrong. A still frame under the distortion looks like the TV of the
///   game at a high speed, and it uses no network and no processor time.
/// `superSpeed` is the flag of the clock of the game; it gives the super speed also when the mode is speed 3.
[[nodiscard]] Playback SpeedPolicy(ClockSpeedMode mode, bool superSpeed, const SpeedRules& rules);

} // namespace ts4_mod::youtube_tv
