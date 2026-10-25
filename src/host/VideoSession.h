/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <atomic>
#include <memory>
#include <optional>
#include <string>

#include <common/Result.h>
#include <core/HostCommand.h>
#include <core/HostProtocol.h>
#include <host/WinHandle.h>

#include "include/cef_audio_handler.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"

namespace ts4_mod::youtube_tv::host
{

class Host;

/// One video: one windowless browser with the player page, and the shared memory with the audio and the frames.
///
/// Threads:
/// - The UI thread of CEF calls the commands, `OnPaint` and the life span callbacks. Only that thread writes frames.
/// - The audio thread of CEF calls `OnAudioStreamPacket`. Only that thread writes into the audio ring.
class VideoSession:
    public CefClient,
    public CefRenderHandler,
    public CefAudioHandler,
    public CefDisplayHandler,
    public CefLifeSpanHandler,
    public CefLoadHandler,
    public CefRequestHandler
{
public:
	/// Make the shared memory, and copy its handle into the parent. The browser does not exist yet: call `Start`.
	[[nodiscard]] static ts4::Result<CefRefPtr<VideoSession>> Make(Host& host, const HostCommand& open);

	/// Make the browser. CEF makes it on the UI thread later and calls `OnAfterCreated`.
	[[nodiscard]] ts4::Result<void> Start();

	[[nodiscard]] std::int32_t Id() const { return _open.id; }
	/// The page with the values of this video. The IO thread of CEF reads it.
	[[nodiscard]] const std::string& Page() const { return _page; }

	void Play();
	void Pause();
	void SetRate(double rate);
	void Seek(double seconds);
	/// Close the browser. When CEF closed it, the session tells the host (`Host::OnSessionClosed`).
	void Close();

	// CefClient
	CefRefPtr<CefAudioHandler> GetAudioHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

	// CefRenderHandler
	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirtyRects, const void* buffer,
	             int width, int height) override;

	// CefAudioHandler
	bool GetAudioParameters(CefRefPtr<CefBrowser> browser, CefAudioParameters& params) override;
	void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser, const CefAudioParameters& params, int channels) override;
	void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser, const float** data, int frames, int64_t pts) override;
	void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override;
	void OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString& message) override;

	// CefDisplayHandler
	bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message,
	                      const CefString& source, int line) override;

	// CefLifeSpanHandler
	bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popupId, const CefString& targetUrl,
	                   const CefString& targetFrameName, cef_window_open_disposition_t targetDisposition, bool userGesture,
	                   const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo, CefRefPtr<CefClient>& client,
	                   CefBrowserSettings& settings, CefRefPtr<CefDictionaryValue>& extraInfo,
	                   bool* noJavascriptAccess) override;
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

	// CefLoadHandler
	void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString& errorText,
	                 const CefString& failedUrl) override;

	// CefRequestHandler
	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
	                    bool userGesture, bool isRedirect) override;
	void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status, int errorCode,
	                               const CefString& errorString) override;

private:
	VideoSession(Host& host, const HostCommand& open);

	/// Run a function of `window.ytv` in the page.
	void CallPage(std::string_view function, std::string_view argument = {});
	/// Send an event of the page to the parent, with the names of the states and the text of the errors.
	void ForwardPageEvent(std::string_view json);

	Host& _host;
	HostCommand _open;
	std::string _nonce;
	std::string _page;
	CefRefPtr<CefBrowser> _browser;
	bool _closing = false;

	UniqueHandle _mapping;
	UniqueView _view;
	std::optional<SharedBlock> _block;

	std::atomic<int> _sourceChannels {0};
	bool _firstFrameSent = false;
	std::uint64_t _convertedFrames = 0;
	std::uint64_t _convertMicroseconds = 0; ///< The sum of the time of all conversions to I420.

	IMPLEMENT_REFCOUNTING(VideoSession);
};

} // namespace ts4_mod::youtube_tv::host
