/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <string>
#include <string_view>

#include <common/Result.h>

namespace ts4_mod::youtube_tv
{

/// The number of characters in the id of a YouTube video.
constexpr std::size_t k_VideoIdLength = 11;

/// One YouTube video that the player can play.
struct YoutubeVideo
{
	std::string id;                ///< The 11 characters of the id, for example `M7lc1UVf-VE`.
	std::uint32_t startSeconds {}; ///< The time to start at, from the `t` or `start` parameter of the URL.

	[[nodiscard]] bool operator==(const YoutubeVideo&) const = default;
};

/// Return true if the text is a correct video id: 11 characters from `A-Z`, `a-z`, `0-9`, `-` and `_`.
[[nodiscard]] bool IsVideoId(std::string_view text);

/// Get the video from a video id or from the URL of a YouTube video.
///
/// The function accepts these forms, with or without `https://` and with other parameters:
/// - `youtube.com/watch?v=<id>` (also `www.`, `m.` and `music.`)
/// - `youtu.be/<id>`
/// - `youtube.com/shorts/<id>`, `youtube.com/embed/<id>`, `youtube.com/live/<id>`
/// - `youtube-nocookie.com/embed/<id>`
///
/// The result is an error for all other text: other sites, playlists without a video, channels and user names.
[[nodiscard]] ts4::Result<YoutubeVideo> ParseYoutubeVideo(std::string_view text);

/// Read the start time of a URL parameter: seconds (`90`, `90s`) or hours, minutes and seconds (`1h2m3s`, `1m30s`).
/// The result is an error for other text.
[[nodiscard]] ts4::Result<std::uint32_t> ParseStartTime(std::string_view text);

} // namespace ts4_mod::youtube_tv
