/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "YoutubeUrl.h"

#include <cctype>

#include <algorithm>
#include <array>
#include <optional>

namespace ts4_mod::youtube_tv
{
namespace
{

/// The hosts that serve a YouTube video page with the id in the path or in the `v` parameter.
constexpr std::array k_YoutubeHosts {
    std::string_view {"youtube.com"},          std::string_view {"www.youtube.com"},
    std::string_view {"m.youtube.com"},        std::string_view {"music.youtube.com"},
    std::string_view {"youtube-nocookie.com"}, std::string_view {"www.youtube-nocookie.com"},
};

/// The hosts of the short links, with the id as the full path.
constexpr std::array k_ShortHosts {std::string_view {"youtu.be"}, std::string_view {"www.youtu.be"}};

/// The first parts of a path that come before the id.
constexpr std::array k_IdPathPrefixes {std::string_view {"/shorts/"}, std::string_view {"/embed/"}, std::string_view {"/live/"},
                                       std::string_view {"/v/"}};

constexpr std::uint32_t k_MaxStartSeconds = 24U * 60U * 60U * 100U;

[[nodiscard]] bool IsIdCharacter(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

[[nodiscard]] std::string ToLower(std::string_view text)
{
	std::string lower(text);
	std::ranges::transform(lower, lower.begin(),
	                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	return lower;
}

[[nodiscard]] std::string_view Trim(std::string_view text)
{
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
	{
		return {};
	}
	const auto last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

[[nodiscard]] bool StartsWithNoCase(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && ToLower(text.substr(0, prefix.size())) == prefix;
}

/// Get the value of one parameter of a query (the text after `?`), or nothing if the query does not have it.
[[nodiscard]] std::optional<std::string_view> QueryValue(std::string_view query, std::string_view name)
{
	while (!query.empty())
	{
		const auto end = query.find('&');
		const auto pair = query.substr(0, end);
		const auto equals = pair.find('=');
		if (pair.substr(0, equals) == name)
		{
			return equals == std::string_view::npos ? std::string_view {} : pair.substr(equals + 1);
		}
		if (end == std::string_view::npos)
		{
			break;
		}
		query.remove_prefix(end + 1);
	}
	return std::nullopt;
}

/// Get the id from the part of a path after a prefix. One `/` after the id is permitted.
[[nodiscard]] std::optional<std::string_view> IdFromPathRest(std::string_view rest)
{
	if (rest.ends_with('/'))
	{
		rest.remove_suffix(1);
	}
	if (!IsVideoId(rest))
	{
		return std::nullopt;
	}
	return rest;
}

/// Get the start time from the query, or 0 if the query does not have a correct start time.
[[nodiscard]] std::uint32_t StartFromQuery(std::string_view query)
{
	for (const auto name : {std::string_view {"t"}, std::string_view {"start"}})
	{
		if (const auto value = QueryValue(query, name))
		{
			if (auto seconds = ParseStartTime(*value))
			{
				return *seconds;
			}
		}
	}
	return 0;
}

} // namespace

bool IsVideoId(std::string_view text)
{
	return text.size() == k_VideoIdLength && std::ranges::all_of(text, IsIdCharacter);
}

ts4::Result<std::uint32_t> ParseStartTime(std::string_view text)
{
	if (text.empty())
	{
		return ts4::MakeError("the start time is empty");
	}
	std::uint64_t total = 0;
	std::uint64_t number = 0;
	bool hasDigits = false;
	for (const char c : text)
	{
		if (c >= '0' && c <= '9')
		{
			number = number * 10 + static_cast<std::uint64_t>(c - '0');
			hasDigits = true;
			if (number > k_MaxStartSeconds)
			{
				return ts4::MakeError("the start time is too large");
			}
			continue;
		}
		if (!hasDigits)
		{
			return ts4::MakeError("the start time \"{}\" is not correct", text);
		}
		std::uint64_t unit = 0;
		switch (c)
		{
		case 'h':
			unit = 3600;
			break;
		case 'm':
			unit = 60;
			break;
		case 's':
			unit = 1;
			break;
		default:
			return ts4::MakeError("the start time \"{}\" is not correct", text);
		}
		total += number * unit;
		number = 0;
		hasDigits = false;
	}
	total += number;
	if (total > k_MaxStartSeconds)
	{
		return ts4::MakeError("the start time is too large");
	}
	return static_cast<std::uint32_t>(total);
}

ts4::Result<YoutubeVideo> ParseYoutubeVideo(std::string_view text)
{
	text = Trim(text);
	if (IsVideoId(text))
	{
		return YoutubeVideo {.id = std::string(text)};
	}

	auto rest = text;
	for (const auto scheme : {std::string_view {"https://"}, std::string_view {"http://"}})
	{
		if (StartsWithNoCase(rest, scheme))
		{
			rest.remove_prefix(scheme.size());
			break;
		}
	}

	const auto hostEnd = rest.find_first_of("/?#");
	const auto host = ToLower(rest.substr(0, hostEnd));
	if (host.empty() || host.find_first_of(":@") != std::string::npos)
	{
		return ts4::MakeError("the text \"{}\" is not the URL of a YouTube video", text);
	}
	rest = hostEnd == std::string_view::npos ? std::string_view {} : rest.substr(hostEnd);
	if (const auto fragment = rest.find('#'); fragment != std::string_view::npos)
	{
		rest = rest.substr(0, fragment);
	}
	const auto queryStart = rest.find('?');
	const auto path = rest.substr(0, queryStart);
	const auto query = queryStart == std::string_view::npos ? std::string_view {} : rest.substr(queryStart + 1);

	std::optional<std::string_view> id;
	if (std::ranges::find(k_ShortHosts, host) != k_ShortHosts.end())
	{
		if (path.starts_with('/'))
		{
			id = IdFromPathRest(path.substr(1));
		}
	}
	else if (std::ranges::find(k_YoutubeHosts, host) != k_YoutubeHosts.end())
	{
		if (path == "/watch" || path == "/watch/")
		{
			if (const auto value = QueryValue(query, "v"); value && IsVideoId(*value))
			{
				id = value;
			}
		}
		for (const auto prefix : k_IdPathPrefixes)
		{
			if (!id && path.starts_with(prefix))
			{
				id = IdFromPathRest(path.substr(prefix.size()));
			}
		}
	}
	else
	{
		return ts4::MakeError("the site \"{}\" is not YouTube", host);
	}

	if (!id)
	{
		return ts4::MakeError("the URL \"{}\" does not have the id of a YouTube video", text);
	}
	return YoutubeVideo {.id = std::string(*id), .startSeconds = StartFromQuery(query)};
}

} // namespace ts4_mod::youtube_tv
