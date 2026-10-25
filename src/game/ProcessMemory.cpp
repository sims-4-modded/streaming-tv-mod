/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "ProcessMemory.h"

#include <cstring>

#include <windows.h>

namespace ts4_mod::youtube_tv
{

namespace
{

constexpr DWORD k_ReadableProtect =
    PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD k_WritableProtect = PAGE_READWRITE | PAGE_EXECUTE_READWRITE;

/// True if each page of the range has committed memory with one of the protections, and no page is a guard page.
[[nodiscard]] bool PagesAllow(std::uintptr_t address, std::size_t size, DWORD protect)
{
	if (address == 0 || size == 0 || address + size < address)
	{
		return false;
	}
	const auto end = address + size;
	auto current = address;
	while (current < end)
	{
		MEMORY_BASIC_INFORMATION info {};
		if (::VirtualQuery(reinterpret_cast<const void*>(current), &info, sizeof(info)) == 0)
		{
			return false;
		}
		if (info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0 || (info.Protect & protect) == 0)
		{
			return false;
		}
		current = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
	}
	return true;
}

} // namespace

bool ProcessMemory::Read(std::uintptr_t address, std::span<std::byte> out) const
{
	if (!PagesAllow(address, out.size(), k_ReadableProtect))
	{
		return false;
	}
	std::memcpy(out.data(), reinterpret_cast<const void*>(address), out.size());
	return true;
}

std::span<std::uint8_t> ProcessMemory::Writable(std::uintptr_t address, std::size_t size) const
{
	if (!PagesAllow(address, size, k_WritableProtect))
	{
		return {};
	}
	return {reinterpret_cast<std::uint8_t*>(address), size};
}

} // namespace ts4_mod::youtube_tv
