/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>

#include <core/Sessions.h>
#include <core/SpeedPolicy.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;
using host::CommandType;

constexpr std::uint64_t k_Tv = 0x1234;
constexpr std::uint64_t k_OtherTv = 0x5678;
constexpr const char* k_Url = "https://youtu.be/M7lc1UVf-VE?t=30";

[[nodiscard]] host::HostEvent Event(std::string type, std::int32_t id, nlohmann::json data = nlohmann::json::object())
{
	return host::HostEvent {.type = std::move(type), .id = id, .data = std::move(data)};
}

[[nodiscard]] std::vector<CommandType> Types(const SessionActions& actions)
{
	std::vector<CommandType> types;
	for (const auto& command : actions.commands)
	{
		types.push_back(command.type);
	}
	return types;
}

TEST(SpeedPolicy, FollowsTheClockOfTheGame)
{
	const SpeedRules rules;
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::Paused, false, rules), (Playback {.play = false, .rate = 1.0, .muted = true}));
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::Normal, false, rules), (Playback {.play = true, .rate = 1.0, .muted = false}));
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::Speed2, false, rules), (Playback {.play = true, .rate = 2.0, .muted = false}));
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::Speed3, false, rules), (Playback {.play = true, .rate = 2.0, .muted = false}));
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::InteractionStartupSpeed, false, rules),
	          (Playback {.play = true, .rate = 1.0, .muted = false}));
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::SuperSpeed3, false, rules), (Playback {.play = false, .rate = 1.0, .muted = true}));
}

TEST(SpeedPolicy, TheSuperSpeedFlagPausesSpeed3)
{
	const SpeedRules rules;
	EXPECT_FALSE(SpeedPolicy(ClockSpeedMode::Speed3, true, rules).play);
	// A paused clock stays paused.
	EXPECT_FALSE(SpeedPolicy(ClockSpeedMode::Paused, true, rules).play);
}

TEST(SpeedPolicy, MutesAtTheMuteLevel)
{
	const SpeedRules rules {.speed2Rate = 1.5, .speed3Rate = 2.0, .muteLevel = 3};
	EXPECT_FALSE(SpeedPolicy(ClockSpeedMode::Speed2, false, rules).muted);
	EXPECT_EQ(SpeedPolicy(ClockSpeedMode::Speed2, false, rules).rate, 1.5);
	const auto speed3 = SpeedPolicy(ClockSpeedMode::Speed3, false, rules);
	EXPECT_TRUE(speed3.play);
	EXPECT_TRUE(speed3.muted);
}

TEST(SpeedPolicy, ReadsTheModes)
{
	EXPECT_EQ(ToClockSpeedMode(5), ClockSpeedMode::SuperSpeed3);
	EXPECT_FALSE(ToClockSpeedMode(6));
	EXPECT_EQ(ParseClockSpeedMode("speed2"), ClockSpeedMode::Speed2);
	EXPECT_EQ(ParseClockSpeedMode("4"), ClockSpeedMode::InteractionStartupSpeed);
	EXPECT_FALSE(ParseClockSpeedMode("fast"));
	EXPECT_FALSE(ParseClockSpeedMode(""));
	EXPECT_FALSE(ParseClockSpeedMode("1x"));
	EXPECT_STREQ(ClockSpeedModeName(ClockSpeedMode::Paused), "paused");
}

TEST(Sessions, PlayOpensAVideoInTheHelper)
{
	SessionManager manager({});
	const auto actions = manager.Play(k_Tv, k_Url);
	ASSERT_TRUE(actions) << actions.GetError().message;
	ASSERT_EQ(actions->commands.size(), 1U);
	const auto& open = actions->commands[0];
	EXPECT_EQ(open.type, CommandType::Open);
	EXPECT_EQ(open.videoId, "M7lc1UVf-VE");
	EXPECT_EQ(open.startSeconds, 30U);
	EXPECT_EQ(open.width, 640U);
	EXPECT_EQ(open.height, 368U);
	const auto* session = manager.Find(k_Tv);
	ASSERT_NE(session, nullptr);
	EXPECT_EQ(session->helperId, open.id);
	EXPECT_EQ(session->state, SessionState::Opening);
}

TEST(Sessions, PlayRefusesABadUrlAndAFullLimit)
{
	SessionManager manager({.maxSessions = 1});
	EXPECT_FALSE(manager.Play(k_Tv, "https://example.com/video"));
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	EXPECT_FALSE(manager.Play(k_OtherTv, k_Url));
	// A new video on the same TV replaces the old one, also at the limit.
	const auto replaced = manager.Play(k_Tv, "dQw4w9WgXcQ");
	ASSERT_TRUE(replaced) << replaced.GetError().message;
	EXPECT_EQ(Types(*replaced), (std::vector {CommandType::Close, CommandType::Open}));
	EXPECT_EQ(replaced->released.size(), 1U);
	EXPECT_EQ(manager.Sessions().size(), 1U);
}

TEST(Sessions, ReadyAppliesTheSpeedOfTheGame)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto id = manager.Find(k_Tv)->helperId;
	EXPECT_TRUE(manager.SetSpeed(ClockSpeedMode::Speed2, false).commands.empty());
	EXPECT_TRUE(manager.OnEvent(Event("opened", id)).commands.empty());
	EXPECT_EQ(manager.Find(k_Tv)->state, SessionState::Loading);
	const auto ready = manager.OnEvent(Event("ready", id));
	ASSERT_EQ(Types(ready), (std::vector {CommandType::SetRate, CommandType::Play}));
	EXPECT_EQ(ready.commands[0].rate, 2.0);
	// The same speed again sends nothing.
	EXPECT_TRUE(manager.SetSpeed(ClockSpeedMode::Speed3, false).commands.empty());
	EXPECT_EQ(Types(manager.SetSpeed(ClockSpeedMode::Paused, false)), (std::vector {CommandType::Pause}));
	EXPECT_EQ(Types(manager.SetSpeed(ClockSpeedMode::Normal, false)), (std::vector {CommandType::SetRate, CommandType::Play}));
	EXPECT_EQ(Types(manager.SetSpeed(ClockSpeedMode::SuperSpeed3, false)), (std::vector {CommandType::Pause}));
	EXPECT_EQ(Types(manager.SetSpeed(ClockSpeedMode::InteractionStartupSpeed, false)), (std::vector {CommandType::Play}));
}

TEST(Sessions, NewSpeedRulesApplyAtOnce)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	(void)manager.SetSpeed(ClockSpeedMode::Speed2, false);
	(void)manager.OnEvent(Event("ready", manager.Find(k_Tv)->helperId));
	const auto actions = manager.SetSettings({.speed = {.speed2Rate = 1.5}});
	ASSERT_EQ(Types(actions), (std::vector {CommandType::SetRate}));
	EXPECT_EQ(actions.commands[0].rate, 1.5);
}

TEST(Sessions, PausesAgainIfThePlayerStartsByItself)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto id = manager.Find(k_Tv)->helperId;
	(void)manager.SetSpeed(ClockSpeedMode::Paused, false);
	EXPECT_EQ(Types(manager.OnEvent(Event("ready", id))), (std::vector {CommandType::Pause}));
	EXPECT_EQ(Types(manager.OnEvent(Event("state", id, {{"state", "playing"}}))), (std::vector {CommandType::Pause}));
	EXPECT_EQ(manager.Find(k_Tv)->state, SessionState::Playing);
}

TEST(Sessions, AnErrorOfThePlayerStopsTheVideo)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto id = manager.Find(k_Tv)->helperId;
	(void)manager.OnEvent(Event("error", id, {{"source", "command"}, {"message", "bad"}}));
	EXPECT_NE(manager.Find(k_Tv)->state, SessionState::Failed);
	(void)manager.OnEvent(Event("error", id, {{"source", "player"}, {"message", "embedding is not permitted"}, {"code", 150}}));
	EXPECT_EQ(manager.Find(k_Tv)->state, SessionState::Failed);
	EXPECT_EQ(manager.Find(k_Tv)->error, "player: embedding is not permitted");
	EXPECT_TRUE(manager.OnEvent(Event("ready", id)).commands.empty());
}

TEST(Sessions, StopClosesTheVideo)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	EXPECT_FALSE(manager.Stop(k_OtherTv));
	const auto stopped = manager.Stop(k_Tv);
	ASSERT_TRUE(stopped);
	EXPECT_EQ(Types(*stopped), (std::vector {CommandType::Close}));
	EXPECT_EQ(manager.Find(k_Tv), nullptr);
}

TEST(Sessions, StopAllClosesEachVideo)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	ASSERT_TRUE(manager.Play(k_OtherTv, k_Url));
	const auto actions = manager.StopAll();
	EXPECT_EQ(actions.released.size(), 2U);
	EXPECT_TRUE(manager.Sessions().empty());
}

TEST(Sessions, TheClosedEventRemovesTheSession)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto id = manager.Find(k_Tv)->helperId;
	const auto actions = manager.OnEvent(Event("closed", id));
	EXPECT_EQ(actions.released, (std::vector {id}));
	EXPECT_TRUE(manager.Sessions().empty());
	EXPECT_TRUE(manager.OnEvent(Event("closed", id)).released.empty());
}

TEST(Sessions, MatchesTheChannelOfTheObject)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.BindChannel(k_Tv, 0xA000);
	manager.BindChannel(k_OtherTv, 0xB000);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
	EXPECT_EQ(manager.MatchChannel(0xB000, std::nullopt), nullptr);
	EXPECT_EQ(manager.MatchChannel(0xC000, std::nullopt), nullptr);
	EXPECT_EQ(manager.MatchChannel(0, std::nullopt), nullptr);
	// The memory of a removed channel goes to a new channel of another object.
	manager.BindChannel(k_OtherTv, 0xA000);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), nullptr);
}

TEST(Sessions, MatchesTheScreenKeyWhenTheChannelIsNotKnown)
{
	SessionManager manager({});
	const ScreenKey key {.instance = 0x8A50833624769271, .type = 0x376840D7, .group = 0};
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.SetScreenKey(k_Tv, key);
	EXPECT_EQ(manager.MatchChannel(0xA000, ScreenKey {.instance = 1, .type = 0x376840D7}), nullptr);
	EXPECT_EQ(manager.MatchChannel(0xA000, key), manager.Find(k_Tv));
	// The manager keeps the channel: the match works now also without the key.
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

constexpr ScreenKey k_News {.instance = 0x5D630A58015C3E89, .type = 0x545AC67A, .group = 0x00BF458B};
constexpr ScreenKey k_Music {.instance = 0x0D432B7F5C14E35D, .type = 0x376840D7, .group = 0};

using namespace std::chrono_literals;

/// The time of the lists in the tests of the clip rule. The link calls come later than the window of the time rule.
constexpr auto k_ListTime = 1000ms;
constexpr auto k_Later = 60000ms;

/// The object that the call linked, with the rule that it used.
std::optional<std::uint64_t> Link(SessionManager& manager, std::uintptr_t channel, const ScreenKey& key,
                                  std::chrono::milliseconds now = k_Later,
                                  SessionManager::LinkRule rule = SessionManager::LinkRule::Clip)
{
	const auto linked = manager.LinkChannelByClip(channel, key, now);
	if (!linked)
	{
		return std::nullopt;
	}
	EXPECT_EQ(linked->rule, rule);
	return linked->objectId;
}

TEST(Sessions, ThePlaylistOfTheObjectLinksTheChannelThatPlaysItsClip)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_Music, k_News}, k_ListTime);
	// A channel with a clip that is not in the list stays free.
	EXPECT_EQ(Link(manager, 0xA000, ScreenKey {.instance = 1, .type = 2}), std::nullopt);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), nullptr);
	EXPECT_EQ(Link(manager, 0xA000, k_News), k_Tv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
	// The link stays: the second call makes no new link.
	EXPECT_EQ(Link(manager, 0xA000, k_News), std::nullopt);
	EXPECT_EQ(Link(manager, 0xA000, k_Music), std::nullopt);
	EXPECT_EQ(Link(manager, 0, k_News), std::nullopt);
}

TEST(Sessions, AShownPlaylistDoesNotTakeASecondChannel)
{
	SessionManager manager({});
	manager.NotePlaylist(k_Tv, {k_News}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_News), k_Tv);
	// Another channel plays the same clip, and no other object got it: the link does not move.
	EXPECT_EQ(Link(manager, 0xB000, k_News, k_ListTime + 10ms), std::nullopt);
	EXPECT_EQ(Link(manager, 0xA000, k_News), std::nullopt);
}

TEST(Sessions, TwoTvsWithTheSameClipGetTheirOwnChannels)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_OtherTv, {k_News}, k_ListTime);
	manager.NotePlaylist(k_Tv, {k_News}, k_ListTime);
	// The newest list comes first.
	EXPECT_EQ(Link(manager, 0xA000, k_News), k_Tv);
	EXPECT_EQ(Link(manager, 0xB000, k_News), k_OtherTv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
	EXPECT_EQ(manager.MatchChannel(0xB000, std::nullopt), nullptr);
}

TEST(Sessions, AnObjectWithNoChannelComesFirst)
{
	SessionManager manager({});
	manager.NotePlaylist(k_OtherTv, {k_News}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xB000, k_News), k_OtherTv);
	// The first TV has a channel and gets a new list. The other TV has a list that no channel played.
	manager.BindChannel(k_Tv, 0xA000);
	manager.NotePlaylist(k_Tv, {k_News}, k_ListTime);
	manager.NotePlaylist(k_OtherTv, {k_Music}, k_ListTime);
	manager.NotePlaylist(0x9999, {k_Music}, k_ListTime);
	manager.NotePlaylist(k_Tv, {k_Music}, k_ListTime);
	// A new channel plays the music clip: it goes to an object with no channel, not to the first TV.
	EXPECT_EQ(Link(manager, 0xC000, k_Music), 0x9999U);
	// The channel of the first TV plays its new clip: the link stays.
	EXPECT_EQ(Link(manager, 0xA000, k_Music), std::nullopt);
}

TEST(Sessions, ANewChannelAtTheAddressOfAnOldChannelMovesTheLink)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_OtherTv, {k_Music}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_Music), k_OtherTv);
	// The game removed the channel of the other TV and made the channel of our TV at the same address.
	manager.NotePlaylist(k_Tv, {k_News}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_News), k_Tv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

TEST(Sessions, AnEmptyListKeepsTheLink)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_News}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_News), k_Tv);
	manager.NotePlaylist(k_Tv, {}, k_Later);
	EXPECT_EQ(Link(manager, 0xA000, k_News, k_Later + 10ms), std::nullopt);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

// The clip of the channel of a TV in the game: not the clip of its list.
constexpr ScreenKey k_TvClip {.instance = 0xEA6FC4038E0F1EC3, .type = 0x376840D7, .group = 0};
constexpr ScreenKey k_TvClip2 {.instance = 0x1234, .type = 0x376840D7, .group = 0};
constexpr auto k_Time = SessionManager::LinkRule::Time;

TEST(Sessions, ANewClipJustAfterAListLinksTheChannelByTheTime)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_News}, 5000ms);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 5016ms, k_Time), k_Tv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
	// The same clip, and the next clip of the channel with no new list: the link stays.
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 5050ms), std::nullopt);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip2, 9000ms), std::nullopt);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

TEST(Sessions, TheTimeRuleNeedsANewClipInTheWindow)
{
	SessionManager manager({});
	// The channel plays its clip before the list.
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 4000ms), std::nullopt);
	manager.NotePlaylist(k_Tv, {k_News}, 5000ms);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 5016ms), std::nullopt);
	// A new channel after the window.
	EXPECT_EQ(Link(manager, 0xB000, k_TvClip, 5000ms + SessionManager::k_LinkWindow + 1ms), std::nullopt);
	// An empty list (the TV turns off) does not link.
	manager.NotePlaylist(k_OtherTv, {}, 7000ms);
	EXPECT_EQ(Link(manager, 0xC000, k_TvClip, 7010ms), std::nullopt);
	// A new channel in the window links.
	manager.NotePlaylist(k_Tv, {k_News}, 8000ms);
	EXPECT_EQ(Link(manager, 0xC000, k_TvClip2, 8020ms, k_Time), k_Tv);
}

TEST(Sessions, TheTimeRuleTakesTheNewestListThatNoChannelShowed)
{
	SessionManager manager({});
	manager.NotePlaylist(k_OtherTv, {k_News}, 5000ms);
	manager.NotePlaylist(k_Tv, {k_News}, 5010ms);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 5020ms, k_Time), k_Tv);
	EXPECT_EQ(Link(manager, 0xB000, k_TvClip2, 5030ms, k_Time), k_OtherTv);
	// The owner of a channel gets a new list, and its channel starts a new clip: the link stays.
	manager.NotePlaylist(k_Tv, {k_News}, 9000ms);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip2, 9020ms), std::nullopt);
	// A clip of a list goes before the time rule.
	manager.NotePlaylist(k_OtherTv, {k_Music}, 12000ms);
	manager.NotePlaylist(0x9999, {k_News}, 12005ms);
	EXPECT_EQ(Link(manager, 0xC000, k_Music, 12010ms), k_OtherTv);
}

// The playlist resources of the TV channels, and their VP6 clips. The clip k_SurfAction is in two playlists.
constexpr ScreenKey k_Action {.instance = 0xEFE29AB011F5411A, .type = k_PlaylistType, .group = 0x00BF458B};
constexpr ScreenKey k_Surf {.instance = 0x0BADC0FFEE000001, .type = k_PlaylistType, .group = 0x00BF458B};
constexpr ScreenKey k_ActionClip2 {.instance = 0xF8C9E12DB961C34A, .type = 0x376840D7, .group = 0};
constexpr ScreenKey k_SurfAction {.instance = 0xAF0F3D9C9F8C858A, .type = 0x376840D7, .group = 0};
constexpr ScreenKey k_SurfClip {.instance = 0x5555, .type = 0x376840D7, .group = 0};
constexpr auto k_ByPlaylist = SessionManager::LinkRule::Playlist;

void AddTvPlaylists(SessionManager& manager)
{
	ASSERT_TRUE(manager.SetPlaylistClips(k_Action, {k_TvClip, k_ActionClip2, k_SurfAction}));
	ASSERT_TRUE(manager.SetPlaylistClips(k_Surf, {k_SurfClip, k_SurfAction}));
	EXPECT_EQ(manager.PlaylistResourceCount(), 2U);
}

TEST(Sessions, APlaylistResourceLinksTheChannelByItsClip)
{
	SessionManager manager({});
	AddTvPlaylists(manager);
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_Action}, k_ListTime);
	// A clip that is in no playlist resource of the list does not link.
	EXPECT_EQ(Link(manager, 0xA000, k_SurfClip), std::nullopt);
	// Rule 2: the playlist resource of the list has the clip. The time of the list is old, so the time rule cannot link.
	EXPECT_EQ(Link(manager, 0xB000, k_TvClip, k_Later, k_ByPlaylist), k_Tv);
	EXPECT_EQ(manager.MatchChannel(0xB000, std::nullopt), manager.Find(k_Tv));
	EXPECT_EQ(manager.Status()["playlistResources"], 2);
}

TEST(Sessions, TheLinkStaysOnTheNextClipOfThePlaylist)
{
	SessionManager manager({});
	AddTvPlaylists(manager);
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_Action}, k_ListTime);
	manager.NotePlaylist(k_OtherTv, {k_Surf}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_ActionClip2, k_Later, k_ByPlaylist), k_Tv);
	// Rule 1: the channel goes to the next clip of the same playlist, and the link stays.
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, k_Later + 30000ms), std::nullopt);
	EXPECT_EQ(Link(manager, 0xA000, k_SurfAction, k_Later + 60000ms), std::nullopt);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

TEST(Sessions, AClipInTwoPlaylistsUsesTheOrderOfRule2)
{
	SessionManager manager({});
	AddTvPlaylists(manager);
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_OtherTv, {k_Surf}, k_ListTime);
	manager.NotePlaylist(k_Tv, {k_Action}, k_ListTime);
	// Both lists have the clip, and no object has a channel: the newest list comes first.
	EXPECT_EQ(Link(manager, 0xA000, k_SurfAction, k_Later, k_ByPlaylist), k_Tv);
	// The second channel with the same clip goes to the other TV, because the list of the first TV is shown.
	EXPECT_EQ(Link(manager, 0xB000, k_SurfAction, k_Later, k_ByPlaylist), k_OtherTv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
	EXPECT_EQ(manager.MatchChannel(0xB000, std::nullopt), nullptr);
	// The next clips stay on their channels.
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip), std::nullopt);
	EXPECT_EQ(Link(manager, 0xB000, k_SurfClip), std::nullopt);
}

TEST(Sessions, AClipKeyInTheListGoesBeforeAPlaylistResource)
{
	SessionManager manager({});
	AddTvPlaylists(manager);
	manager.NotePlaylist(k_Tv, {k_Action, k_SurfClip}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_SurfClip), k_Tv);
}

TEST(Sessions, RefusesABadPlaylistResource)
{
	SessionManager manager({});
	// The playlist key must have the type of a playlist.
	EXPECT_FALSE(manager.SetPlaylistClips(k_Music, {k_TvClip}));
	// A clip key of the type 0, or a playlist in a playlist.
	EXPECT_FALSE(manager.SetPlaylistClips(k_Action, {k_TvClip, ScreenKey {.instance = 1}}));
	EXPECT_FALSE(manager.SetPlaylistClips(k_Action, {k_Surf}));
	// Too many clips.
	const std::vector<ScreenKey> tooMany(SessionManager::k_MaxPlaylistClips + 1, k_TvClip);
	EXPECT_FALSE(manager.SetPlaylistClips(k_Action, tooMany));
	EXPECT_EQ(manager.PlaylistResourceCount(), 0U);
	// The limit of resources. A known resource can change when the limit is full.
	for (std::uint64_t index = 0; index < SessionManager::k_MaxPlaylistResources; ++index)
	{
		ASSERT_TRUE(manager.SetPlaylistClips(ScreenKey {.instance = index, .type = k_PlaylistType}, {k_TvClip}));
	}
	EXPECT_FALSE(manager.SetPlaylistClips(k_Action, {k_TvClip}));
	EXPECT_TRUE(manager.SetPlaylistClips(ScreenKey {.instance = 0, .type = k_PlaylistType}, {k_ActionClip2}));
	// An empty list removes a resource.
	EXPECT_TRUE(manager.SetPlaylistClips(ScreenKey {.instance = 0, .type = k_PlaylistType}, {}));
	EXPECT_EQ(manager.PlaylistResourceCount(), SessionManager::k_MaxPlaylistResources - 1);
	EXPECT_TRUE(manager.SetPlaylistClips(k_Action, {k_TvClip}));
}

TEST(Sessions, AFailedChangeKeepsTheOldClips)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.SetPlaylistClips(k_Action, {k_TvClip}));
	EXPECT_FALSE(manager.SetPlaylistClips(k_Action, {ScreenKey {.instance = 1}}));
	manager.NotePlaylist(k_Tv, {k_Action}, k_ListTime);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, k_Later, k_ByPlaylist), k_Tv);
}

TEST(Sessions, TheTimeRuleWorksWithNoPlaylistResources)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	manager.NotePlaylist(k_Tv, {k_Action}, 5000ms);
	EXPECT_EQ(Link(manager, 0xA000, k_TvClip, 5016ms, k_Time), k_Tv);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_Tv));
}

TEST(Sessions, TheTimeRuleStaysForAClipThatIsInNoResource)
{
	SessionManager manager({});
	AddTvPlaylists(manager);
	manager.NotePlaylist(k_Tv, {k_Action}, 5000ms);
	// The game can have a playlist resource that the script mod did not read, for example of a new pack.
	EXPECT_EQ(Link(manager, 0xA000, k_Music, 5016ms, k_Time), k_Tv);
}

TEST(Sessions, AnyObjectTakesTheFirstFreeChannel)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_OtherTv, k_Url));
	manager.BindChannel(k_OtherTv, 0xB000);
	ASSERT_TRUE(manager.Play(k_AnyObject, k_Url));
	EXPECT_EQ(manager.MatchChannel(0xB000, std::nullopt), manager.Find(k_OtherTv));
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_AnyObject));
	EXPECT_EQ(manager.MatchChannel(0xD000, std::nullopt), nullptr);
	EXPECT_EQ(manager.MatchChannel(0xA000, std::nullopt), manager.Find(k_AnyObject));
}

TEST(Sessions, KeepsTheTransformOfAnObject)
{
	SessionManager manager({});
	const ObjectTransform transform {.position = {1.0F, 2.0F, 3.0F}, .orientation = {0.0F, 0.7071F, 0.0F, 0.7071F}};
	ASSERT_TRUE(manager.Play(k_Tv, k_Url, transform));
	EXPECT_EQ(manager.TransformOf(k_Tv), transform);
	EXPECT_FALSE(manager.TransformOf(k_OtherTv));
}

TEST(Sessions, TheHelperStopFailsEachVideo)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto actions = manager.OnHelperStopped("the pipe closed");
	EXPECT_EQ(actions.released.size(), 1U);
	EXPECT_EQ(manager.Find(k_Tv)->state, SessionState::Failed);
}

TEST(Sessions, TheStatusHasEachVideo)
{
	SessionManager manager({});
	ASSERT_TRUE(manager.Play(k_Tv, k_Url));
	const auto status = manager.Status();
	EXPECT_EQ(status["speed"], "normal");
	ASSERT_EQ(status["videos"].size(), 1U);
	EXPECT_EQ(status["videos"][0]["videoId"], "M7lc1UVf-VE");
	EXPECT_EQ(status["videos"][0]["state"], "opening");
}

} // namespace
