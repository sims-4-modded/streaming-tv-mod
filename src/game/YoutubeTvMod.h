/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <common/Result.h>
#include <core/ThreadGate.h>
#include <core/YoutubeTvSettings.h>
#include <mod_sdk/ModHost.h>

#include "game/TvRuntime.h"

namespace ts4_mod::youtube_tv
{

/// The YouTube TV in the game. It lives from `Ts4ModInit` to `Ts4ModShutdown`. It gives the settings, the hook
/// callbacks, the queries and the status to the extender, and sends each of them to the `TvRuntime`.
class YoutubeTvMod
{
public:
	/// Register the settings, the callbacks and the queries. If the mod cannot start, it writes the reason to the log
	/// and the result is an error.
	[[nodiscard]] static ts4::Result<std::unique_ptr<YoutubeTvMod>> Start(std::unique_ptr<sdk::ModHost> host);

	YoutubeTvMod(const YoutubeTvMod&) = delete;
	YoutubeTvMod& operator=(const YoutubeTvMod&) = delete;
	~YoutubeTvMod();

	/// The runtime for the bridge of the script mod.
	[[nodiscard]] TvRuntime& Runtime() { return *_runtime; }
	/// True on the thread of the game Python. The bridge accepts calls only on that thread. The first call of the bridge
	/// gives the thread, and the mod writes it to the log one time. The game Python does not run on the main thread of
	/// the extender.
	[[nodiscard]] bool OnPythonThread();
	/// The thread of the game Python, or 0 before the first call of the bridge.
	[[nodiscard]] std::uint32_t PythonThread() const { return _pythonGate.Thread(); }

	/// The last error of the bridge of the script mod.
	[[nodiscard]] const std::string& BridgeError() const { return _bridgeError; }
	void SetBridgeError(std::string error) { _bridgeError = std::move(error); }

private:
	explicit YoutubeTvMod(std::unique_ptr<sdk::ModHost> host);

	void RegisterCallbacks();
	/// Post the task that keeps the main thread and adds the Python module of the mod.
	void PostMainThreadTask();
	/// Keep the main thread, and add the Python module. If the interpreter is not ready, post the task again.
	void OnMainThreadTask();
	void RegisterQueries();
	/// Read the offsets from the code of the game, one time. The mod calls it in `Ts4ModInit`, before the extender
	/// puts its hooks on the functions. If the exe is not decrypted yet, the callbacks call it again.
	void EnsureLayouts();

	std::unique_ptr<sdk::ModHost> _host;
	std::unique_ptr<TvRuntime> _runtime;
	/// The client main thread: the thread of the first task of the extender, or 0 before it.
	std::atomic<std::uint32_t> _mainThread {0};
	ThreadGate _pythonGate;
	std::atomic<bool> _refusalLogged {false};
	std::uint64_t _pythonTries = 0;
	bool _layoutsRead = false;
	bool _layoutWaitLogged = false;
	std::string _bridgeError;
};

/// The mod between `Ts4ModInit` and `Ts4ModShutdown`, or null. The bridge of the script mod uses it.
[[nodiscard]] YoutubeTvMod* ActiveYoutubeTvMod();

} // namespace ts4_mod::youtube_tv
