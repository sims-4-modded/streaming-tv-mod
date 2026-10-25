/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "YoutubeTvMod.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include <mod_core/GameVersion.h>

namespace ts4_mod::youtube_tv
{

namespace
{

constexpr const char* k_SettingsFileName = "youtube_tv.json";

[[nodiscard]] std::filesystem::path PathOf(const std::string& utf8)
{
	return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

/// Read the settings file. A file that is not there gives the defaults.
[[nodiscard]] ts4::Result<YoutubeTvSettings> ReadSettings(const sdk::ModHost& host)
{
	const auto path = PathOf(host.SettingsFolder()) / k_SettingsFileName;
	std::error_code error;
	if (!std::filesystem::exists(path, error))
	{
		return YoutubeTvSettings {};
	}
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return ts4::MakeError("cannot open the settings file {}", path.string());
	}
	const std::string text {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	return ParseYoutubeTvSettingsText(text);
}

} // namespace

ts4::Result<std::unique_ptr<YoutubeTvMod>> YoutubeTvMod::Start(std::unique_ptr<sdk::ModHost> host)
{
	auto settings = ReadSettings(*host);
	if (!settings)
	{
		host->Error("cannot start: {}", settings.GetError().message);
		return settings.GetError();
	}
	host->Info("start on the game version {}, settings {}", ToString(host->Version()), YoutubeTvSettingsJson(*settings).dump());
	return std::unique_ptr<YoutubeTvMod>(new YoutubeTvMod(std::move(host), *settings));
}

YoutubeTvMod::YoutubeTvMod(std::unique_ptr<sdk::ModHost> host, YoutubeTvSettings settings)
    : _host(std::move(host))
    , _settings(settings)
{
}

YoutubeTvMod::~YoutubeTvMod()
{
	_host->Info("stop");
}

} // namespace ts4_mod::youtube_tv
