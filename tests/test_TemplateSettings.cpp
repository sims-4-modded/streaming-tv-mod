/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <core/TemplateSettings.h>
#include <gtest/gtest.h>

using ts4_mod::template_mod::ParseTemplateSettings;
using ts4_mod::template_mod::ParseTemplateSettingsText;
using ts4_mod::template_mod::TemplateSettings;
using ts4_mod::template_mod::TemplateSettingsJson;

TEST(TemplateSettings, EmptyTextGivesTheDefaults)
{
	const auto settings = ParseTemplateSettingsText("  \n");
	ASSERT_TRUE(settings);
	EXPECT_EQ(*settings, TemplateSettings {});
}

TEST(TemplateSettings, AMissingFieldKeepsItsDefault)
{
	const auto settings = ParseTemplateSettingsText(R"({"strength": 2.5})");
	ASSERT_TRUE(settings) << settings.GetError().message;
	EXPECT_TRUE(settings->enabled);
	EXPECT_DOUBLE_EQ(settings->strength, 2.5);
}

TEST(TemplateSettings, RefusesAWrongType)
{
	EXPECT_FALSE(ParseTemplateSettingsText(R"({"enabled": 1})"));
	EXPECT_FALSE(ParseTemplateSettingsText(R"({"strength": "high"})"));
	EXPECT_FALSE(ParseTemplateSettingsText("[]"));
}

TEST(TemplateSettings, RefusesAStrengthOutOfRange)
{
	EXPECT_FALSE(ParseTemplateSettingsText(R"({"strength": -1})"));
	EXPECT_FALSE(ParseTemplateSettingsText(R"({"strength": 10.5})"));
}

TEST(TemplateSettings, RefusesBadJson)
{
	EXPECT_FALSE(ParseTemplateSettingsText("{"));
}

TEST(TemplateSettings, WritesWhatItReads)
{
	const TemplateSettings settings {.enabled = false, .strength = 7.0};
	const auto back = ParseTemplateSettings(TemplateSettingsJson(settings));
	ASSERT_TRUE(back);
	EXPECT_EQ(*back, settings);
}
