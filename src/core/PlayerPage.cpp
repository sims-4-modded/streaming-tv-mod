/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "PlayerPage.h"

#include <algorithm>
#include <format>

#include <core/YoutubeUrl.h>

namespace ts4_mod::youtube_tv::host
{
namespace
{

[[nodiscard]] bool IsNonce(std::string_view text)
{
	return text.size() == k_NonceLength &&
	       std::ranges::all_of(text, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

/// Replace each occurrence of `name` with `value`. Returns false if the text does not have `name`.
bool ReplaceAll(std::string& text, std::string_view name, std::string_view value)
{
	bool found = false;
	for (auto at = text.find(name); at != std::string::npos; at = text.find(name, at + value.size()))
	{
		text.replace(at, name.size(), value);
		found = true;
	}
	return found;
}

} // namespace

ts4::Result<std::string> MakePlayerPage(std::string_view pageTemplate, const PlayerPageValues& values)
{
	if (!IsNonce(values.nonce))
	{
		return ts4::MakeError("the nonce is not correct");
	}
	if (!IsVideoId(values.videoId))
	{
		return ts4::MakeError("the video id \"{}\" is not correct", values.videoId);
	}
	std::string page(pageTemplate);
	const auto start = std::to_string(values.startSeconds);
	for (const auto& [name, value] : {std::pair {std::string_view {"{{NONCE}}"}, std::string_view {values.nonce}},
	                                  std::pair {std::string_view {"{{VIDEO_ID}}"}, std::string_view {values.videoId}},
	                                  std::pair {std::string_view {"{{START}}"}, std::string_view {start}}})
	{
		if (!ReplaceAll(page, name, value))
		{
			return ts4::MakeError("the template of the page does not have {}", name);
		}
	}
	return page;
}

std::string MakeNonce(std::uint64_t randomHigh, std::uint64_t randomLow)
{
	return std::format("{:016x}{:016x}", randomHigh, randomLow);
}

std::optional<std::string_view> PageMessage(std::string_view consoleText, std::string_view nonce)
{
	if (nonce.empty() || !consoleText.starts_with(nonce))
	{
		return std::nullopt;
	}
	consoleText.remove_prefix(nonce.size());
	if (!consoleText.starts_with('{'))
	{
		return std::nullopt;
	}
	return consoleText;
}

} // namespace ts4_mod::youtube_tv::host
