/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <windows.h>

namespace ts4_mod::youtube_tv::host
{

/// Closes a Windows handle. A null handle and `INVALID_HANDLE_VALUE` stay open.
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

/// A Windows handle that this object owns.
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

/// Removes the view of a file mapping.
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

/// The text of the last Windows error, in English if possible.
[[nodiscard]] std::string LastErrorText();

/// Change UTF-8 text to UTF-16 text.
[[nodiscard]] std::wstring Widen(std::string_view text);

/// Change UTF-16 text to UTF-8 text.
[[nodiscard]] std::string Narrow(std::wstring_view text);

} // namespace ts4_mod::youtube_tv::host
