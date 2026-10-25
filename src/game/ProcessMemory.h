/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <core/GameObjects.h>

namespace ts4_mod::youtube_tv
{

/// The memory of this process. Each read and each write checks the pages first, so that a bad pointer of the game
/// gives an empty result and not an access violation.
class ProcessMemory final: public MemoryAccess
{
public:
	[[nodiscard]] bool Read(std::uintptr_t address, std::span<std::byte> out) const override;
	[[nodiscard]] std::span<std::uint8_t> Writable(std::uintptr_t address, std::size_t size) const override;
};

} // namespace ts4_mod::youtube_tv
