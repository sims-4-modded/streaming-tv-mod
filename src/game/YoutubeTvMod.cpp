/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "YoutubeTvMod.h"

#include <algorithm>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <Windows.h>
#include <core/BridgeArgs.h>
#include <core/VideoLayout.h>
#include <mod_core/GameVersion.h>
#include <mod_game/Module.h>
#include <mod_game/Scan.h>

#include "game/ProcessMemory.h"
#include "game/PythonModule.h"

namespace ts4_mod::youtube_tv
{

namespace
{

constexpr const char* k_SettingsSection = "youtube_tv";

/// The bytes of code from `start`, at most `size`, in the section of the module that holds `start`.
[[nodiscard]] std::span<const std::byte> CodeWindow(const ModuleImage& module, const std::byte* start, std::size_t size)
{
	for (const auto& section : module.Sections())
	{
		const auto* begin = section.bytes.data();
		const auto* end = begin + section.bytes.size();
		if (start >= begin && start < end)
		{
			return {start, std::min<std::size_t>(size, static_cast<std::size_t>(end - start))};
		}
	}
	return {};
}

template <std::size_t N>
[[nodiscard]] sdk::FunctionRef GameFunction(const char* name, const FixedSignature<N>& signature, uint32_t parameters)
{
	return sdk::FunctionRef {
	    .module = TS4_MODULE_GAME, .name = name, .signature = std::string(signature.text), .parameterCount = parameters};
}

} // namespace

ts4::Result<std::unique_ptr<YoutubeTvMod>> YoutubeTvMod::Start(std::unique_ptr<sdk::ModHost> host)
{
	auto mod = std::unique_ptr<YoutubeTvMod>(new YoutubeTvMod(std::move(host)));
	auto& self = *mod;
	if (!self._host->AddSettings(k_SettingsSection, [&self](const nlohmann::json& json) -> ts4::Result<void> {
		    auto settings = ParseYoutubeTvSettings(json);
		    if (!settings)
		    {
			    return settings.GetError();
		    }
		    self._runtime->ApplySettings(*settings);
		    self._host->Info("settings {}", YoutubeTvSettingsJson(*settings).dump());
		    return {};
	    }))
	{
		self._host->Warn("the extender refused the settings section {}; the mod uses the defaults", k_SettingsSection);
	}
	self.RegisterCallbacks();
	self.RegisterQueries();
	if (!self._host->SetStatusReader([&self] { return self._runtime->Status(); }))
	{
		self._host->Warn("the extender refused the status reader");
	}
	// The extender puts its hooks on the functions after the inits. A hook changes the first bytes of a function,
	// and then the signatures do not match. Thus the mod reads the offsets now.
	self.EnsureLayouts();
	// A task runs on the main thread. The bridge accepts only calls on that thread.
	self.PostMainThreadTask();
	self._host->Info("start on the game version {}", ToString(self._host->Version()));
	return mod;
}

YoutubeTvMod::YoutubeTvMod(std::unique_ptr<sdk::ModHost> host)
    : _host(std::move(host))
{
	auto* raw = _host.get();
	_runtime = std::make_unique<TvRuntime>(
	    YoutubeTvSettings {}, [raw](Ts4LogLevel level, const std::string& text) { raw->Log(level, text); },
	    std::make_unique<ProcessMemory>());
}

YoutubeTvMod::~YoutubeTvMod()
{
	python_module::Unregister();
	_runtime.reset();
	_host->Info("stop");
}

void YoutubeTvMod::PostMainThreadTask()
{
	const bool posted = _host->PostTask([] {
		if (auto* active = ActiveYoutubeTvMod(); active != nullptr)
		{
			active->OnMainThreadTask();
		}
	});
	if (!posted)
	{
		_host->Warn("the extender refused the task on the main thread; the bridge and the Python module stay off");
	}
}

void YoutubeTvMod::OnMainThreadTask()
{
	_mainThread = static_cast<std::uint32_t>(::GetCurrentThreadId());
	++_pythonTries;
	auto registered = python_module::Register();
	if (!registered)
	{
		_host->Error("the Python module {} stays off: {}", k_PythonModuleName, registered.GetError().message);
		return;
	}
	if (!*registered)
	{
		// The interpreter is not ready. The task runs again at the next tick.
		PostMainThreadTask();
		return;
	}
	_host->Info("the Python module {} is in sys.modules (try {}, on the main thread {})", k_PythonModuleName, _pythonTries,
	            _mainThread.load());
}

bool YoutubeTvMod::OnPythonThread()
{
	const auto thread = static_cast<std::uint32_t>(::GetCurrentThreadId());
	switch (_pythonGate.Enter(thread))
	{
	case ThreadGate::Entry::First:
		_host->Info("the bridge takes calls on thread {}, the thread of the game Python (the main thread is {})", thread,
		            _mainThread.load());
		return true;
	case ThreadGate::Entry::Accepted:
		return true;
	case ThreadGate::Entry::Refused:
		break;
	}
	if (!_refusalLogged.exchange(true))
	{
		_host->Warn("the bridge refused a call on thread {}; it takes calls only on thread {}", thread, _pythonGate.Thread());
	}
	return false;
}

void YoutubeTvMod::EnsureLayouts()
{
	if (_layoutsRead)
	{
		return;
	}
	_layoutsRead = true;
	auto module = ModuleImage::Find(TS4_MODULE_GAME);
	if (!module)
	{
		_host->Error("the offsets: {}", module.GetError().message);
		return;
	}
	if (auto decrypted = CheckDecrypted(*module); !decrypted)
	{
		_layoutsRead = false;
		if (!std::exchange(_layoutWaitLogged, true))
		{
			_host->Warn("the offsets wait: {}", decrypted.GetError().message);
		}
		return;
	}
	const auto window = [&](std::string_view name, std::string_view signature, std::size_t size) -> std::span<const std::byte> {
		auto match = FindUniqueSignature(*module, signature, name);
		if (!match)
		{
			_host->Error("the offsets: {}", match.GetError().message);
			return {};
		}
		return CodeWindow(*module, *match, size);
	};
	namespace layout = game_layout;
	const auto renderFrameCode = window("RenderFrame", layout::k_RenderFrameSignature.text, layout::k_RenderFrameWindow);
	if (auto renderer = layout::ReadRendererLayout(renderFrameCode))
	{
		_runtime->SetRendererLayout(*renderer);
	}
	else
	{
		_host->Error("the frames stay off: {}", renderer.GetError().message);
	}
	const auto codeAt = [&](std::uintptr_t address, std::size_t size) -> std::span<const std::byte> {
		const auto* start = reinterpret_cast<const std::byte*>(address);
		if (!module->Contains(start))
		{
			return {};
		}
		return CodeWindow(*module, start, size);
	};
	if (auto settings =
	        layout::ReadVideoSettingsLayout(renderFrameCode, reinterpret_cast<std::uintptr_t>(renderFrameCode.data()), codeAt))
	{
		_runtime->SetVideoSettingsLayout(*settings);
	}
	else
	{
		_host->Warn("the diagnostic of the video settings stays off: {}", settings.GetError().message);
	}
	auto channel = layout::ReadChannelLayout(
	    window("SetPlaylist", layout::k_SetPlaylistSignature.text, layout::k_SetPlaylistWindow),
	    window("PlaylistMessage", layout::k_PlaylistMessageSignature.text, layout::k_PlaylistMessageWindow),
	    window("ChannelUpdate", layout::k_ChannelUpdateSignature.text, layout::k_ChannelUpdateWindow));
	if (channel)
	{
		_runtime->SetChannelLayout(*channel);
	}
	else
	{
		_host->Error("the match of a TV by its object and its clip key stays off: {}", channel.GetError().message);
	}
	if (auto listener =
	        layout::ReadListenerLayout(window("Listener", layout::k_ListenerSignature.text, layout::k_ListenerWindow)))
	{
		_runtime->SetListenerOffset(*listener);
	}
	else
	{
		_host->Error("the listener of the 3D audio stays off: {}", listener.GetError().message);
	}
	_host->Info("the offsets are read: {}", _runtime->Status()["hooks"].dump());
}

void YoutubeTvMod::RegisterCallbacks()
{
	namespace layout = game_layout;
	const auto add = [this](const sdk::FunctionRef& function, Ts4HookPhase phase, std::string_view name,
	                        std::function<void(const Ts4Call&)> callback) {
		if (!_host->AddFunctionCallback(function, phase, name, std::move(callback)))
		{
			_host->Error("the extender refused the callback {} of {}", name, function.name);
		}
	};
	add(GameFunction("VideoRenderer::RenderFrame", layout::k_RenderFrameSignature, 2), TS4_HOOK_BEFORE, "writeFrame",
	    [this](const Ts4Call& call) {
		    EnsureLayouts();
		    _runtime->OnRenderFrame(call.arguments[0], call.arguments[1]);
	    });
	add(GameFunction("HandleVideoSetPlaylist", layout::k_PlaylistMessageSignature, 1), TS4_HOOK_BEFORE, "readObject",
	    [this](const Ts4Call& call) {
		    EnsureLayouts();
		    _runtime->OnPlaylistMessage(call.arguments[0]);
	    });
	add(GameFunction("HandleVideoSetPlaylist", layout::k_PlaylistMessageSignature, 1), TS4_HOOK_AFTER, "forgetObject",
	    [this](const Ts4Call&) { _runtime->OnPlaylistMessageDone(); });
	add(GameFunction("VideoComponent::SetPlaylist", layout::k_SetPlaylistSignature, 1), TS4_HOOK_BEFORE, "readChannel",
	    [this](const Ts4Call& call) {
		    EnsureLayouts();
		    _runtime->OnSetPlaylist(call.arguments[0]);
	    });
	add(GameFunction("UpdateListenerFromCamera", layout::k_ListenerSignature, 1), TS4_HOOK_AFTER, "readListener",
	    [this](const Ts4Call& call) {
		    EnsureLayouts();
		    _runtime->OnListener(call.arguments[0]);
	    });
}

void YoutubeTvMod::RegisterQueries()
{
	const auto add = [this](std::string_view name, std::string_view help,
	                        std::function<nlohmann::json(const nlohmann::json&)> query) {
		if (!_host->AddQuery(name, help, std::move(query)))
		{
			_host->Error("the extender refused the query {}", name);
		}
	};
	const auto check = [](const auto& result) {
		if (!result)
		{
			throw std::runtime_error(result.GetError().message);
		}
	};
	add("youtube_tv.play", "Play a video on a TV: {objectId?: number, text or \"any\", url, position?, orientation?}",
	    [this, check](const nlohmann::json& params) {
		    const auto objectId = bridge::ReadObjectIdField(params, k_AnyObject);
		    check(objectId);
		    if (!params.contains("url") || !params["url"].is_string())
		    {
			    throw std::runtime_error("the parameter \"url\" is necessary");
		    }
		    std::optional<ObjectTransform> transform;
		    if (params.contains("position"))
		    {
			    auto read = bridge::ReadTransformFields(params);
			    check(read);
			    transform = *read;
		    }
		    check(_runtime->Play(*objectId, params["url"].get<std::string>(), transform));
		    return _runtime->Status();
	    });
	add("youtube_tv.stop", "Stop the video of a TV, or each video: {objectId?}", [this, check](const nlohmann::json& params) {
		if (params.is_object() && params.contains("objectId"))
		{
			const auto objectId = bridge::ReadObjectIdField(params, std::nullopt);
			check(objectId);
			return nlohmann::json {{"stopped", _runtime->Stop(*objectId)}};
		}
		_runtime->StopAll();
		return nlohmann::json {{"stopped", "all"}};
	});
	add("youtube_tv.status", "The videos, the helper, the hooks and the audio",
	    [this](const nlohmann::json&) { return _runtime->Status(); });
	add("youtube_tv.speed", "Give the clock speed: {mode: paused|normal|speed2|speed3|startup|super or 0..5, superSpeed?}",
	    [this](const nlohmann::json& params) {
		    if (!params.is_object() || !params.contains("mode"))
		    {
			    throw std::runtime_error("the parameter \"mode\" is necessary");
		    }
		    const auto& field = params["mode"];
		    const auto mode = field.is_string()            ? ParseClockSpeedMode(field.get<std::string>())
		                      : field.is_number_unsigned() ? ToClockSpeedMode(field.get<std::uint32_t>())
		                                                   : std::nullopt;
		    if (!mode)
		    {
			    throw std::runtime_error("the mode is not known");
		    }
		    const bool superSpeed = params.value("superSpeed", *mode == ClockSpeedMode::SuperSpeed3);
		    _runtime->SetSpeed(*mode, superSpeed);
		    return _runtime->Status();
	    });
	add("youtube_tv.transform", "Give the position of a TV for the audio: {objectId, position, orientation?}",
	    [this, check](const nlohmann::json& params) {
		    const auto objectId = bridge::ReadObjectIdField(params, std::nullopt);
		    check(objectId);
		    const auto transform = bridge::ReadTransformFields(params);
		    check(transform);
		    _runtime->SetTransform(*objectId, *transform);
		    return _runtime->Status();
	    });
}

} // namespace ts4_mod::youtube_tv
