/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "VideoSession.h"

#include <chrono>
#include <format>
#include <random>
#include <span>

#include <core/PlayerPage.h>
#include <host/Host.h>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

namespace ts4_mod::youtube_tv::host
{
namespace
{

/// Copy a handle into the parent process. Returns the value of the handle in the parent, or 0 if it failed.
[[nodiscard]] std::uint64_t CopyHandleToParent(HANDLE parent, HANDLE handle)
{
	if (parent == nullptr)
	{
		// The helper runs without a parent (a test by hand): give the value in this process.
		return reinterpret_cast<std::uintptr_t>(handle);
	}
	HANDLE copy = nullptr;
	if (!::DuplicateHandle(::GetCurrentProcess(), handle, parent, &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		return 0;
	}
	return reinterpret_cast<std::uintptr_t>(copy);
}

[[nodiscard]] std::string RandomNonce()
{
	std::random_device random;
	const auto part = [&] { return (std::uint64_t {random()} << 32) | random(); };
	return MakeNonce(part(), part());
}

} // namespace

VideoSession::VideoSession(Host& host, const HostCommand& open)
    : _host(host)
    , _open(open)
    , _nonce(RandomNonce())
{
}

ts4::Result<CefRefPtr<VideoSession>> VideoSession::Make(Host& host, const HostCommand& open)
{
	CefRefPtr<VideoSession> session(new VideoSession(host, open));

	auto page = MakePlayerPage(host.PageTemplate(),
	                           {.nonce = session->_nonce, .videoId = open.videoId, .startSeconds = open.startSeconds});
	if (!page)
	{
		return page.GetError();
	}
	session->_page = std::move(*page);

	const auto bytes = SharedBlockBytes(open.width, open.height);
	session->_mapping.reset(
	    ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(bytes), nullptr));
	if (!session->_mapping)
	{
		return ts4::MakeError("CreateFileMappingW failed: {}", LastErrorText());
	}
	session->_view.reset(::MapViewOfFile(session->_mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, bytes));
	if (!session->_view)
	{
		return ts4::MakeError("MapViewOfFile failed: {}", LastErrorText());
	}
	auto block = SharedBlock::Make({static_cast<std::byte*>(session->_view.get()), bytes}, open.width, open.height, open.matrix,
	                               open.range);
	if (!block)
	{
		return block.GetError();
	}
	session->_block = *block;

	const auto memoryHandle = CopyHandleToParent(host.ParentProcess(), session->_mapping.get());
	if (memoryHandle == 0)
	{
		return ts4::MakeError("DuplicateHandle into the parent failed: {}", LastErrorText());
	}
	host.Events().Send("opened", open.id,
	                   {
	                       {"videoId", open.videoId},
	                       {"width", open.width},
	                       {"height", open.height},
	                       {"format", "i420"},
	                       {"matrix", YuvMatrixName(open.matrix)},
	                       {"range", YuvRangeName(open.range)},
	                       {"sharedMemory", memoryHandle},
	                       {"sharedBytes", bytes},
	                       {"handleInParent", host.ParentProcess() != nullptr},
	                   });
	return session;
}

ts4::Result<void> VideoSession::Start()
{
	CefWindowInfo window;
	window.SetAsWindowless(nullptr);
	// The game needs the frames in memory, so the helper uses OnPaint and not a GPU texture.
	window.shared_texture_enabled = false;

	CefBrowserSettings settings;
	settings.windowless_frame_rate = static_cast<int>(_open.frameRate);
	settings.background_color = CefColorSetARGB(255, 0, 0, 0);

	const auto url = std::format("{}/player/{}.html", k_PlayerPageOrigin, _open.id);
	if (!CefBrowserHost::CreateBrowser(window, this, url, settings, nullptr, nullptr))
	{
		return ts4::MakeError("CefBrowserHost::CreateBrowser failed");
	}
	return {};
}

void VideoSession::CallPage(std::string_view function, std::string_view argument)
{
	if (!_browser)
	{
		_host.Events().SendError(Id(), "command", "the browser of the video does not exist yet");
		return;
	}
	const auto code = std::format("window.ytv && window.ytv.{}({});", function, argument);
	_browser->GetMainFrame()->ExecuteJavaScript(code, std::string(k_PlayerPageUrl), 0);
}

void VideoSession::Play()
{
	CallPage("play");
}

void VideoSession::Pause()
{
	CallPage("pause");
}

void VideoSession::SetRate(double rate)
{
	CallPage("setRate", std::format("{}", rate));
}

void VideoSession::Seek(double seconds)
{
	CallPage("seek", std::format("{}", seconds));
}

void VideoSession::Close()
{
	_closing = true;
	if (_browser)
	{
		_browser->GetHost()->CloseBrowser(true);
	}
}

void VideoSession::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect)
{
	rect = CefRect(0, 0, static_cast<int>(_open.width), static_cast<int>(_open.height));
}

void VideoSession::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width,
                           int height)
{
	if (type != PET_VIEW || !_block || buffer == nullptr || width <= 0 || height <= 0)
	{
		return;
	}
	// CEF gives the full view in each call, so the helper converts the full frame.
	const auto started = std::chrono::steady_clock::now();
	const auto w = static_cast<std::uint32_t>(width);
	const auto h = static_cast<std::uint32_t>(height);
	const BgraImage image {
	    .pixels = {static_cast<const std::uint8_t*>(buffer), std::size_t {w} * h * 4}, .pitch = w * 4, .width = w, .height = h};
	auto& frames = _block->Frames();
	if (!ConvertBgraToI420(image, frames.BackPlanes(), _open.matrix, _open.range))
	{
		return;
	}
	frames.Publish(_host.NowUs());
	_convertMicroseconds += static_cast<std::uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
	++_convertedFrames;
	if (!_firstFrameSent)
	{
		_firstFrameSent = true;
		_host.Events().Send("frame", Id(), {{"frames", frames.FrameCounter()}, {"paintWidth", w}, {"paintHeight", h}});
	}
}

bool VideoSession::GetAudioParameters(CefRefPtr<CefBrowser>, CefAudioParameters& params)
{
	params.channel_layout = CEF_CHANNEL_LAYOUT_STEREO;
	params.sample_rate = static_cast<int>(k_AudioSampleRate);
	return true;
}

void VideoSession::OnAudioStreamStarted(CefRefPtr<CefBrowser>, const CefAudioParameters& params, int channels)
{
	_sourceChannels = channels;
	_block->Audio().StartStream(static_cast<std::uint32_t>(params.sample_rate));
	_host.Events().Send("audio", Id(),
	                    {{"active", true},
	                     {"sampleRate", params.sample_rate},
	                     {"channels", _block->Audio().Channels()},
	                     {"sourceChannels", channels}});
}

void VideoSession::OnAudioStreamPacket(CefRefPtr<CefBrowser>, const float** data, int frames, int64_t)
{
	const int channels = _sourceChannels.load();
	if (data == nullptr || frames <= 0 || channels <= 0)
	{
		return;
	}
	_block->Audio().WritePlanar(std::span<const float* const>(data, static_cast<std::size_t>(channels)),
	                            static_cast<std::size_t>(frames));
}

void VideoSession::OnAudioStreamStopped(CefRefPtr<CefBrowser>)
{
	_host.Events().Send("audio", Id(), {{"active", false}, {"droppedFrames", _block->Audio().DroppedFrames()}});
}

void VideoSession::OnAudioStreamError(CefRefPtr<CefBrowser>, const CefString& message)
{
	_host.Events().SendError(Id(), "audio", message.ToString());
}

bool VideoSession::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message, const CefString&, int)
{
	const auto text = message.ToString();
	const auto json = PageMessage(text, _nonce);
	if (!json)
	{
		return false;
	}
	ForwardPageEvent(*json);
	return true;
}

void VideoSession::ForwardPageEvent(std::string_view text)
{
	auto event = ParseHostEvent(text);
	if (!event)
	{
		return;
	}
	auto& data = event->data;
	data.erase("event");
	data.erase("id");
	if (event->type == "state" || event->type == "time")
	{
		if (const auto state = data.find("state"); state != data.end() && state->is_number_integer())
		{
			data["state"] = PlayerStateName(state->get<int>());
		}
	}
	if (event->type == "error")
	{
		const int code = data.value("code", 0);
		data["source"] = code < 0 ? "page" : "player";
		if (!data.contains("message"))
		{
			data["message"] = PlayerErrorText(code);
		}
	}
	if (event->type == "time")
	{
		data["frames"] = _block->Frames().FrameCounter();
		data["convertMs"] = _convertedFrames == 0
		                        ? 0.0
		                        : static_cast<double>(_convertMicroseconds) / 1000.0 / static_cast<double>(_convertedFrames);
		data["audioFrames"] = _block->Audio().AvailableFrames();
		data["droppedAudioFrames"] = _block->Audio().DroppedFrames();
	}
	_host.Events().Send(event->type, Id(), std::move(data));
}

bool VideoSession::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString&, const CefString&,
                                 cef_window_open_disposition_t, bool, const CefPopupFeatures&, CefWindowInfo&,
                                 CefRefPtr<CefClient>&, CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*)
{
	// No popup windows: the player has no window to show them in.
	return true;
}

void VideoSession::OnAfterCreated(CefRefPtr<CefBrowser> browser)
{
	CEF_REQUIRE_UI_THREAD();
	_browser = browser;
	if (_closing)
	{
		_browser->GetHost()->CloseBrowser(true);
	}
}

void VideoSession::OnBeforeClose(CefRefPtr<CefBrowser>)
{
	CEF_REQUIRE_UI_THREAD();
	_browser = nullptr;
	_host.OnSessionClosed(Id());
}

void VideoSession::OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                               const CefString& errorText, const CefString& failedUrl)
{
	if (!frame->IsMain() || errorCode == ERR_ABORTED)
	{
		return;
	}
	_host.Events().SendError(Id(), "load", std::format("{} ({})", errorText.ToString(), failedUrl.ToString()),
	                         static_cast<int>(errorCode));
}

bool VideoSession::OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool, bool)
{
	// The main frame shows only the player page. The player iframe can go to its own pages.
	if (frame->IsMain() && !request->GetURL().ToString().starts_with(k_PlayerPageOrigin))
	{
		return true;
	}
	return false;
}

void VideoSession::OnRenderProcessTerminated(CefRefPtr<CefBrowser>, TerminationStatus status, int errorCode,
                                             const CefString& errorString)
{
	_host.Events().SendError(
	    Id(), "renderer",
	    std::format("the renderer process stopped (status {}): {}", static_cast<int>(status), errorString.ToString()),
	    errorCode);
}

} // namespace ts4_mod::youtube_tv::host
