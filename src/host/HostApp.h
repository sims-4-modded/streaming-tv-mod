/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <functional>
#include <string>

#include "include/cef_app.h"
#include "include/cef_scheme.h"

namespace ts4_mod::youtube_tv::host
{

class Host;

/// The CEF application of all processes of the helper. In the browser process, it adds the switches of Chromium that
/// the player needs.
class HostApp: public CefApp, public CefBrowserProcessHandler
{
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override;

private:
	IMPLEMENT_REFCOUNTING(HostApp);
};

/// Serves the player page of each video at `https://youtube-tv.invalid/player/<id>.html` from the memory of the helper.
/// The IO thread of CEF calls it.
class PageSchemeFactory: public CefSchemeHandlerFactory
{
public:
	explicit PageSchemeFactory(const Host& host)
	    : _host(host)
	{
	}

	CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& schemeName,
	                                     CefRefPtr<CefRequest> request) override;

private:
	const Host& _host;

	IMPLEMENT_REFCOUNTING(PageSchemeFactory);
};

/// Run a function on the UI thread of CEF. Returns false if CEF does not run.
bool PostToUi(std::function<void()> function);

} // namespace ts4_mod::youtube_tv::host
