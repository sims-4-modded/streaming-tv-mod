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
