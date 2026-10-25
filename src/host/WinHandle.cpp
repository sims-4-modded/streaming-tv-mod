/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "WinHandle.h"

#include <format>

namespace ts4_mod::youtube_tv::host
{

std::string LastErrorText()
{
	const DWORD code = ::GetLastError();
	wchar_t* buffer = nullptr;
	const DWORD length =
	    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
	                     code, MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
	const std::unique_ptr<wchar_t, decltype(&::LocalFree)> owner(buffer, &::LocalFree);
	auto text = length == 0 ? std::string {} : Narrow(std::wstring_view(buffer, length));
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.'))
	{
		text.pop_back();
	}
	return std::format("Windows error {}: {}", code, text);
}

std::wstring Widen(std::string_view text)
{
	if (text.empty())
	{
		return {};
	}
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
	return wide;
}

std::string Narrow(std::wstring_view text)
{
	if (text.empty())
	{
		return {};
	}
	const int length =
	    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string narrow(static_cast<std::size_t>(length), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
	return narrow;
}

} // namespace ts4_mod::youtube_tv::host
