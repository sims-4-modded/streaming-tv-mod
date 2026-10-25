/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <thread>

#include <common/Result.h>
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
	/// True on the thread that called `Ts4ModInit`: the main thread of the game.
	[[nodiscard]] bool OnMainThread() const { return std::this_thread::get_id() == _mainThread; }

	/// The last error of the bridge of the script mod.
	[[nodiscard]] const std::string& BridgeError() const { return _bridgeError; }
	void SetBridgeError(std::string error) { _bridgeError = std::move(error); }

private:
	explicit YoutubeTvMod(std::unique_ptr<sdk::ModHost> host);

	void RegisterCallbacks();
	void RegisterQueries();
	/// Read the offsets from the code of the game, one time. The mod calls it in `Ts4ModInit`, before the extender
	/// puts its hooks on the functions. If the exe is not decrypted yet, the callbacks call it again.
	void EnsureLayouts();

	std::unique_ptr<sdk::ModHost> _host;
	std::unique_ptr<TvRuntime> _runtime;
	std::thread::id _mainThread;
	bool _layoutsRead = false;
	bool _layoutWaitLogged = false;
	std::string _bridgeError;
};

/// The mod between `Ts4ModInit` and `Ts4ModShutdown`, or null. The bridge of the script mod uses it.
[[nodiscard]] YoutubeTvMod* ActiveYoutubeTvMod();

} // namespace ts4_mod::youtube_tv
