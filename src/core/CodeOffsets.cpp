/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "CodeOffsets.h"

#include <mod_core/Signature.h>

namespace ts4_mod::youtube_tv
{

namespace
{

[[nodiscard]] std::size_t BytesOf(DisplacementSize size)
{
	switch (size)
	{
	case DisplacementSize::None:
		return 0;
	case DisplacementSize::Byte:
		return 1;
	case DisplacementSize::Dword:
		return 4;
	}
	return 0;
}

} // namespace

ts4::Result<std::int64_t> ReadDisplacement(std::span<const std::byte> code, const DisplacementRule& rule)
{
	auto signature = Signature::Parse(rule.pattern);
	if (!signature)
	{
		return ts4::MakeError("the pattern of {} is not correct: {}", rule.name, signature.GetError().message);
	}
	const auto bytes = BytesOf(rule.size);
	if (rule.at + bytes > signature->Size())
	{
		return ts4::MakeError("the displacement of {} is not in its pattern", rule.name);
	}
	if (rule.alignment < 1 || rule.min > rule.max)
	{
		return ts4::MakeError("the range of {} is not correct", rule.name);
	}
	auto match = FindUniqueSignature(code, signature->Bytes(), rule.name);
	if (!match)
	{
		return match.GetError();
	}
	const auto start = *match + rule.at;
	std::int64_t value = 0;
	if (bytes == 1)
	{
		value = static_cast<std::int8_t>(code[start]);
	}
	else if (bytes == 4)
	{
		std::uint32_t raw = 0;
		for (std::size_t index = 0; index < 4; ++index)
		{
			raw |= std::uint32_t {std::to_integer<std::uint8_t>(code[start + index])} << (8 * index);
		}
		value = static_cast<std::int32_t>(raw);
	}
	if (value < rule.min || value > rule.max)
	{
		return ts4::MakeError("the value {} of {} is not in the range {} to {}", value, rule.name, rule.min, rule.max);
	}
	if (value % rule.alignment != 0)
	{
		return ts4::MakeError("the value {} of {} is not a multiple of {}", value, rule.name, rule.alignment);
	}
	return value;
}

ts4::Result<std::size_t> ReadFieldOffset(std::span<const std::byte> code, const DisplacementRule& rule)
{
	if (rule.min < 0)
	{
		return ts4::MakeError("the range of the field {} has negative values", rule.name);
	}
	auto value = ReadDisplacement(code, rule);
	if (!value)
	{
		return value.GetError();
	}
	return static_cast<std::size_t>(*value);
}

ts4::Result<std::uintptr_t> ReadRelativeTarget(std::span<const std::byte> code, std::uintptr_t codeAddress,
                                               const RelativeRule& rule)
{
	auto signature = Signature::Parse(rule.pattern);
	if (!signature)
	{
		return ts4::MakeError("the pattern of {} is not correct: {}", rule.name, signature.GetError().message);
	}
	if (rule.at + 4 > rule.end || rule.end > signature->Size())
	{
		return ts4::MakeError("the displacement of {} is not in its pattern", rule.name);
	}
	auto match = FindUniqueSignature(code, signature->Bytes(), rule.name);
	if (!match)
	{
		return match.GetError();
	}
	std::uint32_t raw = 0;
	for (std::size_t index = 0; index < 4; ++index)
	{
		raw |= std::uint32_t {std::to_integer<std::uint8_t>(code[*match + rule.at + index])} << (8 * index);
	}
	const auto displacement = static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
	const auto end = codeAddress + *match + rule.end;
	return static_cast<std::uintptr_t>(static_cast<std::int64_t>(end) + displacement);
}

} // namespace ts4_mod::youtube_tv
