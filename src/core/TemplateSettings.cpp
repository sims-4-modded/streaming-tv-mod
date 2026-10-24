/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "TemplateSettings.h"

namespace ts4_mod::template_mod
{

ts4::Result<TemplateSettings> ParseTemplateSettings(const nlohmann::json& json)
{
	if (!json.is_object())
	{
		return ts4::MakeError("the settings are not a JSON object");
	}
	TemplateSettings settings;
	if (const auto field = json.find("enabled"); field != json.end())
	{
		if (!field->is_boolean())
		{
			return ts4::MakeError("the field \"enabled\" is not true or false");
		}
		settings.enabled = field->get<bool>();
	}
	if (const auto field = json.find("strength"); field != json.end())
	{
		if (!field->is_number())
		{
			return ts4::MakeError("the field \"strength\" is not a number");
		}
		const auto strength = field->get<double>();
		if (strength < k_MinStrength || strength > k_MaxStrength)
		{
			return ts4::MakeError("the field \"strength\" is {}, but it must be from {} to {}", strength, k_MinStrength,
			                      k_MaxStrength);
		}
		settings.strength = strength;
	}
	return settings;
}

ts4::Result<TemplateSettings> ParseTemplateSettingsText(std::string_view text)
{
	if (text.find_first_not_of(" \t\r\n") == std::string_view::npos)
	{
		return TemplateSettings {};
	}
	const auto json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded())
	{
		return ts4::MakeError("the settings file is not correct JSON");
	}
	return ParseTemplateSettings(json);
}

nlohmann::json TemplateSettingsJson(const TemplateSettings& settings)
{
	return {{"enabled", settings.enabled}, {"strength", settings.strength}};
}

} // namespace ts4_mod::template_mod
