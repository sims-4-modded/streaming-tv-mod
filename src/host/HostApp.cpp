/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "HostApp.h"

#include <charconv>
#include <cstring>

#include <algorithm>
#include <string_view>

#include <core/PlayerPage.h>
#include <host/Host.h>

#include "include/cef_stream.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace ts4_mod::youtube_tv::host
{
namespace
{

/// A stream that owns its text, so the text stays while CEF reads it.
class TextReadHandler: public CefReadHandler
{
public:
	explicit TextReadHandler(std::string text)
	    : _text(std::move(text))
	{
	}

	size_t Read(void* ptr, size_t size, size_t n) override
	{
		const auto wanted = size * n;
		const auto count = std::min(wanted, _text.size() - _position);
		std::memcpy(ptr, _text.data() + _position, count);
		_position += count;
		return size == 0 ? 0 : count / size;
	}

	int Seek(int64_t offset, int whence) override
	{
		int64_t base = 0;
		switch (whence)
		{
		case SEEK_SET:
			base = 0;
			break;
		case SEEK_CUR:
			base = static_cast<int64_t>(_position);
			break;
		case SEEK_END:
			base = static_cast<int64_t>(_text.size());
			break;
		default:
			return -1;
		}
		const auto target = base + offset;
		if (target < 0 || target > static_cast<int64_t>(_text.size()))
		{
			return -1;
		}
		_position = static_cast<std::size_t>(target);
		return 0;
	}

	int64_t Tell() override { return static_cast<int64_t>(_position); }
	int Eof() override { return _position >= _text.size() ? 1 : 0; }
	bool MayBlock() override { return false; }

private:
	std::string _text;
	std::size_t _position = 0;

	IMPLEMENT_REFCOUNTING(TextReadHandler);
};

/// A task that runs one function.
class FunctionTask: public CefTask
{
public:
	explicit FunctionTask(std::function<void()> function)
	    : _function(std::move(function))
	{
	}

	void Execute() override { _function(); }

private:
	std::function<void()> _function;

	IMPLEMENT_REFCOUNTING(FunctionTask);
};

/// Get the id of the video from a path `/player/<id>.html`.
[[nodiscard]] std::optional<std::int32_t> PageId(std::string_view url)
{
	constexpr std::string_view k_Prefix = "/player/";
	constexpr std::string_view k_Suffix = ".html";
	if (!url.starts_with(k_PlayerPageOrigin))
	{
		return std::nullopt;
	}
	auto path = url.substr(k_PlayerPageOrigin.size());
	path = path.substr(0, path.find_first_of("?#"));
	if (!path.starts_with(k_Prefix) || !path.ends_with(k_Suffix))
	{
		return std::nullopt;
	}
	path = path.substr(k_Prefix.size(), path.size() - k_Prefix.size() - k_Suffix.size());
	std::int32_t id = 0;
	const auto [end, error] = std::from_chars(path.data(), path.data() + path.size(), id);
	if (error != std::errc {} || end != path.data() + path.size() || id < 1)
	{
		return std::nullopt;
	}
	return id;
}

} // namespace

void HostApp::OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine)
{
	if (!processType.empty())
	{
		return;
	}
	// The player starts without a click of the user.
	commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
	// The media keys of the keyboard must not control the player of the game.
	commandLine->AppendSwitchWithValue("disable-features", "HardwareMediaKeyHandling,MediaSessionService");
	commandLine->AppendSwitch("disable-component-update");
}

CefRefPtr<CefResourceHandler> PageSchemeFactory::Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&,
                                                        CefRefPtr<CefRequest> request)
{
	const auto url = request->GetURL().ToString();
	const auto id = PageId(url);
	auto page = id ? _host.PageFor(*id) : std::nullopt;
	CefResponse::HeaderMap headers;
	headers.emplace("Cache-Control", "no-store");
	if (!page)
	{
		CefRefPtr<CefReadHandler> empty(new TextReadHandler("not found"));
		return new CefStreamResourceHandler(404, "Not Found", "text/plain", headers, CefStreamReader::CreateForHandler(empty));
	}
	CefRefPtr<CefReadHandler> reader(new TextReadHandler(std::move(*page)));
	return new CefStreamResourceHandler(200, "OK", "text/html", headers, CefStreamReader::CreateForHandler(reader));
}

bool PostToUi(std::function<void()> function)
{
	return CefPostTask(TID_UI, new FunctionTask(std::move(function)));
}

} // namespace ts4_mod::youtube_tv::host
