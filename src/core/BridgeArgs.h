/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <string_view>

#include <common/Result.h>
#include <core/Sessions.h>
#include <core/SpeedPolicy.h>

/// The checks of the arguments of the C interface that the script mod calls (`YoutubeTv*` exports, version 1). The
/// script mod calls the interface through `ctypes`, so each pointer can be null and each value can be wrong. Each
/// check returns an error text for the last error of the interface.
namespace ts4_mod::youtube_tv::bridge
{

/// The version of the C interface.
constexpr std::uint32_t k_BridgeVersion = 1;

/// The largest URL that the interface reads, in bytes.
constexpr std::size_t k_MaxUrlBytes = 2048;
/// The largest value of a coordinate of a position that the interface accepts.
constexpr float k_MaxCoordinate = 1.0e5F;

/// Read the URL: not null, not empty, at most `k_MaxUrlBytes` bytes before the terminating zero, and no control
/// characters. The function reads at most `k_MaxUrlBytes + 1` bytes.
[[nodiscard]] ts4::Result<std::string_view> ReadUrl(const char* url);

/// Read a position of three floats: not null, finite, and each value in the limit.
[[nodiscard]] ts4::Result<std::array<float, 3>> ReadPosition(const float* position3);

/// Read a quaternion of four floats (x, y, z, w): not null, finite, not of length 0. The result is a unit quaternion.
[[nodiscard]] ts4::Result<std::array<float, 4>> ReadOrientation(const float* orientation4);

/// Read a transform. Both pointers are necessary.
[[nodiscard]] ts4::Result<ObjectTransform> ReadTransform(const float* position3, const float* orientation4);

/// Read the clip key of a TV. The type must not be 0.
[[nodiscard]] ts4::Result<ScreenKey> ReadScreenKey(std::uint64_t instance, std::uint32_t type, std::uint32_t group);

/// The clock of the game, as the script mod gives it.
struct SpeedArguments
{
	ClockSpeedMode mode = ClockSpeedMode::Normal;
	double scale = 1.0;
	bool superSpeed = false;
};

/// Read the speed: a known `clock.ClockSpeedMode`, a finite scale of 0 or more, and a flag of 0 or 1.
[[nodiscard]] ts4::Result<SpeedArguments> ReadSpeed(std::uint32_t clockSpeedMode, double speedScale, std::uint32_t superSpeed);

/// Copy a text into a buffer of `size` bytes, with a terminating zero, and return the length of the full text. If the
/// buffer is too small, the function copies the first part of the text, and does not divide a UTF-8 character. A null
/// buffer or the size 0 gets no copy.
std::uint32_t CopyText(std::string_view text, char* buffer, std::uint32_t size);

} // namespace ts4_mod::youtube_tv::bridge
