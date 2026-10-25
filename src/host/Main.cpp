/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The helper youtube_tv_host.exe: CEF with windowless rendering and the official YouTube player. The parent (the DLL
// mod) starts it, writes commands to its standard input, and reads events from its standard output. The same exe is
// also each subprocess of CEF (renderer, GPU, audio and network).

#include <charconv>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>

#include <core/HostCommand.h>
#include <core/HostProtocol.h>
#include <core/PlayerPage.h>
#include <host/EventWriter.h>
#include <host/Host.h>
#include <host/HostApp.h>
#include <host/Resource.h>
#include <host/WinHandle.h>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_version.h"

namespace
{

using namespace ts4_mod::youtube_tv::host;

/// Get the template of the player page from the resources of the exe.
[[nodiscard]] std::string LoadPageTemplate()
{
	HRSRC resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PLAYER_PAGE), RT_RCDATA);
	if (resource == nullptr)
	{
		return {};
	}
	HGLOBAL loaded = ::LoadResource(nullptr, resource);
	const auto* data = static_cast<const char*>(::LockResource(loaded));
	if (data == nullptr)
	{
		return {};
	}
	return {data, ::SizeofResource(nullptr, resource)};
}

[[nodiscard]] std::filesystem::path ExeFolder()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;)
	{
		const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length < path.size())
		{
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	return std::filesystem::path(path).parent_path();
}

[[nodiscard]] HostOptions ReadOptions(CefRefPtr<CefCommandLine> commandLine)
{
	HostOptions options;
	if (commandLine->HasSwitch("parent-pid"))
	{
		const auto text = commandLine->GetSwitchValue("parent-pid").ToString();
		std::uint32_t pid = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), pid);
		if (error == std::errc {} && end == text.data() + text.size() && pid != 0)
		{
			options.parentPid = pid;
		}
	}
	options.cacheFolder = commandLine->HasSwitch("cache-dir") ? commandLine->GetSwitchValue("cache-dir").ToWString()
	                                                          : (ExeFolder() / L"cache").wstring();
	return options;
}

/// Read the command lines from the standard input, and run each command on the UI thread. At the end of the input,
/// or after a failed read, the thread asks the host to stop.
void ReadCommands(std::stop_token stop, Host& host, EventWriter& events, std::atomic<bool>& done)
{
	const struct DoneAtEnd
	{
		std::atomic<bool>& done;
		~DoneAtEnd() { done = true; }
	} doneAtEnd {done};
	HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
	std::string buffer;
	std::array<char, 4096> chunk {};
	while (!stop.stop_requested())
	{
		DWORD read = 0;
		if (input == nullptr || input == INVALID_HANDLE_VALUE ||
		    !::ReadFile(input, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) || read == 0)
		{
			break;
		}
		if (stop.stop_requested())
		{
			break;
		}
		buffer.append(chunk.data(), read);
		for (auto end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n'))
		{
			std::string line = buffer.substr(0, end);
			buffer.erase(0, end + 1);
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (line.find_first_not_of(" \t") == std::string::npos)
			{
				continue;
			}
			auto command = ParseHostCommand(line);
			if (!command)
			{
				events.SendError(0, "command", command.GetError().message);
				continue;
			}
			PostToUi([&host, command = *command] { host.Handle(command); });
		}
	}
	if (!stop.stop_requested())
	{
		PostToUi([&host] { host.RequestQuit(); });
	}
}

/// Wait until the parent process stops, then ask the host to stop.
void WatchParent(std::stop_token stop, Host& host, HANDLE stopEvent)
{
	HANDLE parent = host.ParentProcess();
	if (parent == nullptr)
	{
		return;
	}
	const HANDLE handles[] = {parent, stopEvent};
	if (::WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 && !stop.stop_requested())
	{
		PostToUi([&host] { host.RequestQuit(); });
	}
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
	CefMainArgs mainArgs(instance);
	CefRefPtr<HostApp> app(new HostApp());

	// A subprocess of CEF runs here and returns.
	if (const int code = CefExecuteProcess(mainArgs, app, nullptr); code >= 0)
	{
		return code;
	}

	EventWriter events;
	CefRefPtr<CefCommandLine> commandLine = CefCommandLine::CreateCommandLine();
	commandLine->InitFromString(::GetCommandLineW());
	const auto options = ReadOptions(commandLine);
	const auto pageTemplate = LoadPageTemplate();
	if (pageTemplate.empty())
	{
		events.SendError(0, "host", "the exe does not have the player page");
		return 1;
	}

	std::error_code ignored;
	std::filesystem::create_directories(options.cacheFolder, ignored);

	CefSettings settings;
	settings.no_sandbox = true;
	settings.windowless_rendering_enabled = true;
	settings.multi_threaded_message_loop = false;
	settings.background_color = CefColorSetARGB(255, 0, 0, 0);
	settings.log_severity = LOGSEVERITY_WARNING;
	CefString(&settings.root_cache_path) = options.cacheFolder;
	CefString(&settings.log_file) = (std::filesystem::path(options.cacheFolder) / L"cef.log").wstring();
	CefString(&settings.locale) = "en-US";

	if (!CefInitialize(mainArgs, settings, app, nullptr))
	{
		events.SendError(0, "host", std::format("CefInitialize failed with the exit code {}", CefGetExitCode()));
		return 2;
	}

	{
		Host host(events, options, pageTemplate);
		CefRegisterSchemeHandlerFactory("https", std::string(k_PlayerPageHost), new PageSchemeFactory(host));

		events.Send("hello", 0,
		            {{"version", k_ProtocolVersion},
		             {"cef", CEF_VERSION},
		             {"chromium", std::format("{}.{}.{}.{}", CHROME_VERSION_MAJOR, CHROME_VERSION_MINOR, CHROME_VERSION_BUILD,
		                                      CHROME_VERSION_PATCH)},
		             {"pid", ::GetCurrentProcessId()}});

		UniqueHandle stopEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
		{
			std::atomic<bool> readerDone {false};
			std::jthread reader([&](std::stop_token stop) { ReadCommands(stop, host, events, readerDone); });
			std::jthread watcher([&](std::stop_token stop) { WatchParent(stop, host, stopEvent.get()); });

			CefRunMessageLoop();

			// Stop the two threads before CefShutdown: after it, they must not post a task.
			reader.request_stop();
			watcher.request_stop();
			::SetEvent(stopEvent.get());
			// The reader can be before its ReadFile call: cancel the read again until the thread is done.
			for (int attempt = 0; attempt < 200 && !readerDone; ++attempt)
			{
				::CancelSynchronousIo(reader.native_handle());
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if (!readerDone)
			{
				// The read does not stop. The process stops soon, and Windows then stops the thread.
				reader.detach();
			}
		}
		CefClearSchemeHandlerFactories();
		events.Send("bye");
		CefShutdown();
	}
	return 0;
}
