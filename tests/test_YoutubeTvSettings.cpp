/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <core/YoutubeTvSettings.h>
#include <gtest/gtest.h>

using ts4_mod::youtube_tv::ParseYoutubeTvSettings;
using ts4_mod::youtube_tv::ParseYoutubeTvSettingsText;
using ts4_mod::youtube_tv::YoutubeTvSettings;
using ts4_mod::youtube_tv::YoutubeTvSettingsJson;

TEST(YoutubeTvSettings, EmptyTextGivesTheDefaults)
{
	const auto settings = ParseYoutubeTvSettingsText("  \n");
	ASSERT_TRUE(settings);
	EXPECT_EQ(*settings, YoutubeTvSettings {});
}

TEST(YoutubeTvSettings, AMissingFieldKeepsItsDefault)
{
	const auto settings = ParseYoutubeTvSettingsText(R"({"strength": 2.5})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_TRUE(settings->enabled);
	EXPECT_DOUBLE_EQ(settings->strength, 2.5);
}

TEST(YoutubeTvSettings, RefusesAWrongType)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"enabled": 1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"strength": "high"})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText("[]"));
}

TEST(YoutubeTvSettings, RefusesAStrengthOutOfRange)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"strength": -1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"strength": 10.5})"));
}

TEST(YoutubeTvSettings, RefusesBadJson)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText("{"));
}

TEST(YoutubeTvSettings, WritesWhatItReads)
{
	const YoutubeTvSettings settings {.enabled = false, .strength = 7.0};
	const auto back = ParseYoutubeTvSettings(YoutubeTvSettingsJson(settings));
	ASSERT_TRUE(back);
	EXPECT_EQ(*back, settings);
}
