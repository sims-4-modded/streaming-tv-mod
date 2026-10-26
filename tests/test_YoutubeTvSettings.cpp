/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <core/YoutubeTvSettings.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

TEST(YoutubeTvSettings, EmptyTextGivesTheDefaults)
{
	const auto settings = ParseYoutubeTvSettingsText("  \n");
	ASSERT_TRUE(settings);
	EXPECT_EQ(*settings, YoutubeTvSettings {});
	const auto empty = ParseYoutubeTvSettingsText("{}");
	ASSERT_TRUE(empty);
	EXPECT_EQ(*empty, YoutubeTvSettings {});
}

TEST(YoutubeTvSettings, AMissingFieldKeepsItsDefault)
{
	const auto settings = ParseYoutubeTvSettingsText(R"({"volume": 0.5, "matrix": "bt709", "muteLevel": 3})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_TRUE(settings->enabled);
	EXPECT_DOUBLE_EQ(settings->volume, 0.5);
	EXPECT_EQ(settings->matrix, YuvMatrix::Bt709);
	EXPECT_EQ(settings->speed.muteLevel, 3U);
	EXPECT_EQ(settings->width, 640U);
}

TEST(YoutubeTvSettings, RefusesAWrongType)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"enabled": 1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"volume": "high"})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"helperPath": 3})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"maxSessions": 1.5})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"modelFront": [0, 1]})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText("[]"));
}

TEST(YoutubeTvSettings, RefusesAValueOutOfRange)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"volume": -0.1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"volume": 1.1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"maxSessions": 0})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"maxSessions": -1})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"width": 4000})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"speed3Rate": 3})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"muteLevel": 7})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"range": "wide"})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"cameraMatrix": "rows"})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"modelFront": [0, 0, 0]})"));
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"modelFront": [0, 1, 0]})"));
}

TEST(YoutubeTvSettings, RefusesBadJson)
{
	EXPECT_FALSE(ParseYoutubeTvSettingsText("{"));
}

TEST(YoutubeTvSettings, WritesWhatItReads)
{
	YoutubeTvSettings settings;
	settings.enabled = false;
	settings.helperPath = "E:/helper/youtube_tv_host.exe";
	settings.volume = 0.25;
	settings.maxSessions = 3;
	settings.range = YuvRange::Full;
	settings.scaleFilter = ScaleFilter::Nearest;
	settings.speed.speed2Rate = 1.5;
	settings.axes.rightHanded = false;
	settings.axes.cameraLayout = CameraMatrixLayout::ViewColumns;
	settings.axes.modelFront = {1.0F, 0.0F, 0.0F};
	const auto back = ParseYoutubeTvSettings(YoutubeTvSettingsJson(settings));
	ASSERT_TRUE(back) << back.GetError().message;
	EXPECT_EQ(*back, settings);
}

TEST(YoutubeTvSettings, GivesTheSessionSettingsWithSizesOfTheHelper)
{
	YoutubeTvSettings settings;
	settings.width = 650;
	settings.height = 360;
	const auto sessions = SessionSettingsOf(settings);
	EXPECT_EQ(sessions.width, 656U);
	EXPECT_EQ(sessions.height, 368U);
	EXPECT_EQ(sessions.maxSessions, 2U);
}

TEST(YoutubeTvSettings, ReadsTheFrameWriteMode)
{
	const auto settings = ParseYoutubeTvSettingsText(R"({"frameWrite": "bars"})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_EQ(settings->frameWrite, FrameWriteMode::Bars);
	EXPECT_EQ(YoutubeTvSettingsJson(*settings)["frameWrite"], "bars");
	EXPECT_FALSE(ParseYoutubeTvSettingsText(R"({"frameWrite": "pattern"})"));
	EXPECT_EQ(YoutubeTvSettings {}.frameWrite, FrameWriteMode::Video);
}

} // namespace
