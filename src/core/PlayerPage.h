/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <optional>
#include <string>
#include <string_view>

#include <common/Result.h>

/// The local page that holds the YouTube IFrame Player API. The helper keeps the page as a resource of its exe, and
/// serves it at `k_PlayerPageUrl` from its own memory. No server on the internet serves the page.
///
/// The page sends events to the helper as console messages: the nonce of the browser, then one JSON object. The
/// player iframe of YouTube has a different origin and cannot read the nonce, thus it cannot send false events.
namespace ts4_mod::youtube_tv::host
{

/// The origin of the page. The domain `.invalid` never resolves, so no request goes to a server for the page itself.
/// YouTube gets this origin in the `Referer` header and in the `origin` parameter of the player.
constexpr std::string_view k_PlayerPageOrigin = "https://youtube-tv.invalid";
constexpr std::string_view k_PlayerPageHost = "youtube-tv.invalid";
constexpr std::string_view k_PlayerPageUrl = "https://youtube-tv.invalid/player.html";

/// The number of characters of a nonce.
constexpr std::size_t k_NonceLength = 32;

/// The values that the helper puts into the template of the page.
struct PlayerPageValues
{
	std::string nonce;   ///< `k_NonceLength` characters from `a-z` and `0-9`.
	std::string videoId; ///< A correct video id (see `IsVideoId`).
	std::uint32_t startSeconds = 0;
};

/// Put the values into the template: `{{NONCE}}`, `{{VIDEO_ID}}` and `{{START}}`. The result is an error if a value
/// is not correct, or if the template does not have each name.
[[nodiscard]] ts4::Result<std::string> MakePlayerPage(std::string_view pageTemplate, const PlayerPageValues& values);

/// Make a nonce from 128 random bits.
[[nodiscard]] std::string MakeNonce(std::uint64_t randomHigh, std::uint64_t randomLow);

/// Get the JSON text of a console message from the page, or nothing if the message does not start with the nonce.
[[nodiscard]] std::optional<std::string_view> PageMessage(std::string_view consoleText, std::string_view nonce);

} // namespace ts4_mod::youtube_tv::host
