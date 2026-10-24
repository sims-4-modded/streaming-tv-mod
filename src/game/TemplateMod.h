/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <memory>

#include <common/Result.h>
#include <core/TemplateSettings.h>
#include <mod_sdk/ModHost.h>

namespace ts4_mod::template_mod
{

/// The template mod in the game. It lives from `Ts4ModInit` to `Ts4ModShutdown`.
class TemplateMod
{
public:
	/// Read the settings and start the mod. If the mod cannot start, it writes the reason to the log and the result is
	/// an error.
	[[nodiscard]] static ts4::Result<std::unique_ptr<TemplateMod>> Start(std::unique_ptr<sdk::ModHost> host);

	TemplateMod(const TemplateMod&) = delete;
	TemplateMod& operator=(const TemplateMod&) = delete;
	~TemplateMod();

private:
	TemplateMod(std::unique_ptr<sdk::ModHost> host, TemplateSettings settings);

	std::unique_ptr<sdk::ModHost> _host;
	TemplateSettings _settings;
};

} // namespace ts4_mod::template_mod
