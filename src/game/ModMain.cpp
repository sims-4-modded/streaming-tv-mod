/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The entry points of the template mod DLL. The extender loads the DLL from its folder of DLL mods and calls these
// functions on the main thread. No exception goes out of these functions.

#include <exception>
#include <memory>

#include <mod_api/Ts4ModApi.h>
#include <mod_sdk/ModHost.h>

#include "game/TemplateMod.h"

namespace
{

constexpr const char* k_ModId = "mod_template";
constexpr const char* k_ModName = "Template mod";
constexpr const char* k_ModVersion = "0.1.0";

/// The mod. It lives from `Ts4ModInit` to `Ts4ModShutdown`.
std::unique_ptr<ts4_mod::template_mod::TemplateMod> s_mod;

} // namespace

extern "C" __declspec(dllexport) uint32_t Ts4ModApiVersion()
{
	return TS4_MOD_API_VERSION;
}

extern "C" __declspec(dllexport) void Ts4ModGetInfo(Ts4ModInfo* info)
{
	if (info == nullptr)
	{
		return;
	}
	info->id = k_ModId;
	info->name = k_ModName;
	info->version = k_ModVersion;
}

extern "C" __declspec(dllexport) uint32_t Ts4ModInit(const Ts4HostApi* api)
{
	try
	{
		auto host = ts4_mod::sdk::ModHost::Create(api);
		if (!host)
		{
			// Without a correct table, the mod cannot write to the log. The extender writes the reason.
			return 0;
		}
		// The mod writes the reason to the log if it cannot start.
		auto mod = ts4_mod::template_mod::TemplateMod::Start(std::move(*host));
		if (!mod)
		{
			return 0;
		}
		s_mod = std::move(*mod);
		return 1;
	}
	catch (const std::exception&)
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void Ts4ModShutdown()
{
	try
	{
		s_mod.reset();
	}
	catch (const std::exception&)
	{
	}
}
