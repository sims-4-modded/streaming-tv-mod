/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>

#include <windows.h>

namespace ts4_mod::youtube_tv
{

/// Closes a handle of Windows.
struct HandleCloser
{
	void operator()(HANDLE handle) const
	{
		if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(handle);
		}
	}
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

/// Unmaps a view of a file mapping.
struct ViewUnmapper
{
	void operator()(void* view) const
	{
		if (view != nullptr)
		{
			::UnmapViewOfFile(view);
		}
	}
};
using UniqueView = std::unique_ptr<void, ViewUnmapper>;

/// Frees a DLL.
struct LibraryFreer
{
	void operator()(HMODULE module) const
	{
		if (module != nullptr)
		{
			::FreeLibrary(module);
		}
	}
};
using UniqueLibrary = std::unique_ptr<std::remove_pointer_t<HMODULE>, LibraryFreer>;

} // namespace ts4_mod::youtube_tv
