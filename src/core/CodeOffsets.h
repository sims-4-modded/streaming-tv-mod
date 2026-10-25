/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <span>
#include <string_view>

#include <common/Result.h>

/// Read the field offsets of a game object from the code of the game. The mod never types an offset as a number.
/// It finds one instruction of the game that reads or writes the field, and reads the displacement of that
/// instruction. The research notes of the mod give each instruction and its source.
namespace ts4_mod::youtube_tv
{

/// The size of the displacement of an instruction.
enum class DisplacementSize
{
	/// The form of the instruction itself gives a displacement of 0 (for example a read from `[rbx]`). The pattern has
	/// no wildcard for it.
	None,
	Byte,  ///< One byte, with its sign.
	Dword, ///< Four bytes, with their sign.
};

/// One field offset in the code of the game.
struct DisplacementRule
{
	std::string_view name; ///< The name of the field in the log.
	/// The signature text of one or more instructions, with wildcards at the displacement. The pattern must match
	/// exactly one place in the code that the reader gets.
	std::string_view pattern;
	std::size_t at = 0; ///< The index in the pattern of the first byte of the displacement.
	DisplacementSize size = DisplacementSize::Dword;
	std::int64_t min = 0;       ///< The lowest value that the mod accepts.
	std::int64_t max = 0;       ///< The highest value that the mod accepts.
	std::int64_t alignment = 1; ///< The value must be a multiple of this number.
};

/// Find the one match of the pattern of `rule` in `code`, and read its displacement. The result is an error if the
/// pattern is not correct, if it matches no place or more than one place, if the displacement is not in the pattern,
/// or if the value is out of the range or not aligned.
[[nodiscard]] ts4::Result<std::int64_t> ReadDisplacement(std::span<const std::byte> code, const DisplacementRule& rule);

/// Read a displacement and return it as an unsigned offset. The range of the rule must not be negative.
[[nodiscard]] ts4::Result<std::size_t> ReadFieldOffset(std::span<const std::byte> code, const DisplacementRule& rule);

} // namespace ts4_mod::youtube_tv
