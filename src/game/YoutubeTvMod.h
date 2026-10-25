/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>

#include <common/Result.h>
#include <core/YoutubeTvSettings.h>
#include <mod_sdk/ModHost.h>

namespace ts4_mod::youtube_tv
{

/// The YouTube TV in the game. It lives from `Ts4ModInit` to `Ts4ModShutdown`.
class YoutubeTvMod
{
public:
	/// Read the settings and start the mod. If the mod cannot start, it writes the reason to the log and the result is
	/// an error.
	[[nodiscard]] static ts4::Result<std::unique_ptr<YoutubeTvMod>> Start(std::unique_ptr<sdk::ModHost> host);

	YoutubeTvMod(const YoutubeTvMod&) = delete;
	YoutubeTvMod& operator=(const YoutubeTvMod&) = delete;
	~YoutubeTvMod();

private:
	YoutubeTvMod(std::unique_ptr<sdk::ModHost> host, YoutubeTvSettings settings);

	std::unique_ptr<sdk::ModHost> _host;
	YoutubeTvSettings _settings;
};

} // namespace ts4_mod::youtube_tv
