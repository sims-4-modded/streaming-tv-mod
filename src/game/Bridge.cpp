/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The C interface for the script mod (version 1, see `docs/design.md`). The script mod calls it through `ctypes` on the
// main thread, until the native Python module of the extender exists. Each function checks each argument, catches
// each exception, and sets the last error of the interface.

#include "game/Bridge.h"

#include <cstdint>

#include <exception>
#include <format>
#include <functional>
#include <string>

#include <Windows.h>
#include <core/BridgeArgs.h>

#include "game/YoutubeTvMod.h"

namespace
{

using namespace ts4_mod::youtube_tv;

/// The last error when no mod is active.
constexpr const char* k_NotReady = "the DLL is not ready: the extender did not start the mod";

/// Run one call of the interface. `call` returns an error text, or an empty text for a call that succeeded. The
/// result is 1 for success and 0 for an error.
std::uint32_t Call(const std::function<std::string(YoutubeTvMod&)>& call)
{
	auto* mod = ActiveYoutubeTvMod();
	if (mod == nullptr)
	{
		return 0;
	}
	std::string error;
	try
	{
		if (!mod->OnPythonThread())
		{
			error = std::format("the call is on thread {}, but the bridge takes calls only on thread {}",
			                    ::GetCurrentThreadId(), mod->PythonThread());
		}
		else
		{
			error = call(*mod);
		}
	}
	catch (const std::exception& exception)
	{
		error = std::string("an exception: ") + exception.what();
	}
	catch (...)
	{
		error = "an unknown exception";
	}
	try
	{
		mod->SetBridgeError(error);
	}
	catch (...)
	{
	}
	return error.empty() ? 1 : 0;
}

} // namespace

extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvApiVersion()
{
	return bridge::k_BridgeVersion;
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvPlay(std::uint64_t objectId, const char* url,
                                                                     const float* position3, const float* orientation4)
{
	return Call([&](YoutubeTvMod& mod) -> std::string {
		const auto text = bridge::ReadUrl(url);
		if (!text)
		{
			return text.GetError().message;
		}
		std::optional<ObjectTransform> transform;
		if (position3 != nullptr || orientation4 != nullptr)
		{
			auto read = bridge::ReadTransform(position3, orientation4);
			if (!read)
			{
				return read.GetError().message;
			}
			transform = *read;
		}
		auto played = mod.Runtime().Play(objectId, *text, transform);
		return played ? std::string {} : played.GetError().message;
	});
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvStop(std::uint64_t objectId)
{
	return Call([&](YoutubeTvMod& mod) -> std::string {
		return mod.Runtime().Stop(objectId) ? std::string {} : std::string("the TV has no video");
	});
}

extern "C" __declspec(dllexport) void __cdecl YoutubeTvStopAll()
{
	Call([](YoutubeTvMod& mod) -> std::string {
		mod.Runtime().StopAll();
		return {};
	});
}

extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetSpeed(std::uint32_t clockSpeedMode, double speedScale,
                                                                std::uint32_t superSpeed)
{
	Call([&](YoutubeTvMod& mod) -> std::string {
		const auto speed = bridge::ReadSpeed(clockSpeedMode, speedScale, superSpeed);
		if (!speed)
		{
			return speed.GetError().message;
		}
		mod.Runtime().SetSpeed(speed->mode, speed->superSpeed, speed->scale);
		return {};
	});
}

extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetTransform(std::uint64_t objectId, const float* position3,
                                                                    const float* orientation4)
{
	Call([&](YoutubeTvMod& mod) -> std::string {
		const auto transform = bridge::ReadTransform(position3, orientation4);
		if (!transform)
		{
			return transform.GetError().message;
		}
		mod.Runtime().SetTransform(objectId, *transform);
		return {};
	});
}

extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetScreenKey(std::uint64_t objectId, std::uint64_t instance,
                                                                    std::uint32_t type, std::uint32_t group)
{
	Call([&](YoutubeTvMod& mod) -> std::string {
		const auto key = bridge::ReadScreenKey(instance, type, group);
		if (!key)
		{
			return key.GetError().message;
		}
		mod.Runtime().SetScreenKey(objectId, *key);
		return {};
	});
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvLastError(char* buffer, std::uint32_t size)
{
	try
	{
		const auto* mod = ActiveYoutubeTvMod();
		const std::string_view text = mod == nullptr ? std::string_view(k_NotReady) : std::string_view(mod->BridgeError());
		return bridge::CopyText(text, buffer, size);
	}
	catch (...)
	{
		return 0;
	}
}
