/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The local probe of the DLL mod, outside the game. It uses the runtime of the DLL with fake objects of the game in
// its own memory:
// 1. The 3D audio: a test tone from the left, then from the right (no helper is necessary).
// 2. The helper: it plays a video, and the runtime writes the frames into fake planes, through the same callbacks
//    that the game calls. The probe compares the planes with the frame of the helper.
// 3. The speed: pause, the speed 3 and the super speed.
// 4. The audio of the video from the left, then from the right.
//
// Usage: tv_probe [--helper <exe>] [--video <id or URL>] [--tone-only] [--seconds <n>]
// It returns 0 if all checks pass, 1 if a check fails, and 77 if the helper or the audio device is not there.

#include <cmath>
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include <core/GameObjects.h>
#include <core/VideoLayout.h>
#include <windows.h>

#include "game/AudioEngine.h"
#include "game/HostClient.h"
#include "game/ProcessMemory.h"
#include "game/TvRuntime.h"

namespace
{

using namespace ts4_mod::youtube_tv;
using namespace std::chrono_literals;

constexpr int k_Skip = 77;
constexpr std::uint64_t k_Tv = 0x0123456789ABCDEF;
constexpr std::uint8_t k_Guard = 0xEE;

void Print(const std::string& text)
{
	std::printf("%s\n", text.c_str());
	std::fflush(stdout);
}

struct Report
{
	int failed = 0;

	void Check(const std::string& name, bool passed, const std::string& detail = {})
	{
		Print(std::format("[{}] {}{}", passed ? "PASS" : "FAIL", name, detail.empty() ? "" : ": " + detail));
		failed += passed ? 0 : 1;
	}
};

struct Options
{
	std::filesystem::path helper;
	std::string video = "M7lc1UVf-VE";
	bool toneOnly = false;
	int seconds = 8;
};

[[nodiscard]] std::filesystem::path ExeFolder()
{
	std::wstring path(MAX_PATH, L' ');
	const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	path.resize(length);
	return std::filesystem::path(path).parent_path();
}

/// The helper: the option, else the installed helper, else the helper of this build.
[[nodiscard]] std::filesystem::path FindHelper(const Options& options)
{
	if (!options.helper.empty())
	{
		return options.helper;
	}
	if (auto installed = HostClient::DefaultExe(); installed && std::filesystem::exists(*installed))
	{
		return *installed;
	}
	return ExeFolder().parent_path() / L"host" / L"youtube_tv_host.exe";
}

/// A sine tone of 440 Hz for the audio test.
class ToneSource final: public PcmSource
{
public:
	std::size_t Read(std::span<float> out) override
	{
		const auto frames = out.size() / 2;
		for (std::size_t index = 0; index < frames; ++index)
		{
			const auto value =
			    0.2F * std::sin(2.0F * std::numbers::pi_v<float> * 440.0F * static_cast<float>(_frame++) / 48000.0F);
			out[index * 2] = value;
			out[index * 2 + 1] = value;
		}
		return frames;
	}
	[[nodiscard]] std::size_t Available() const override { return 48000; }
	void DropOldest(std::size_t) override {}

private:
	std::uint64_t _frame = 0;
};

/// The listener at the origin, looking along +z of X3DAudio.
const AudioPose k_Listener {.position = {0.0F, 0.0F, 0.0F}, .front = {0.0F, 0.0F, 1.0F}, .top = {0.0F, 1.0F, 0.0F}};

[[nodiscard]] AudioPose EmitterAt(float x)
{
	return {.position = {x, 0.0F, 2.0F}, .front = {0.0F, 0.0F, -1.0F}, .top = {0.0F, 1.0F, 0.0F}};
}

int ToneTest(Report& report)
{
	auto engine = AudioEngine::Start({});
	if (!engine)
	{
		Print("No audio: " + engine.GetError().message);
		return k_Skip;
	}
	auto& audio = **engine;
	report.Check("the audio engine started", true, std::format("{} output channels", audio.OutputChannels()));
	report.Check("the voice of the tone", audio.AddVoice(1, std::make_shared<ToneSource>()).HasValue());
	audio.SetListener(k_Listener);
	for (const auto [name, x] : {std::pair {"left", -2.0F}, std::pair {"right", 2.0F}})
	{
		audio.SetVoice(1, EmitterAt(x), 0.5F);
		audio.Update();
		Print(std::format("  the tone plays from the {} for 1 s", name));
		std::this_thread::sleep_for(1s);
	}
	const auto status = audio.Status();
	const auto submitted = status["voices"][0]["submittedBlocks"].get<std::uint64_t>();
	report.Check("the engine fed the voice", submitted >= 150, std::format("{} blocks of 10 ms", submitted));
	audio.RemoveVoice(1);
	return 0;
}

/// Fake objects of the game: a renderer, a channel, a frame with three planes, a message and a component, with the
/// offsets of `k_Layout`.
class FakeGame
{
public:
	static constexpr game_layout::RendererLayout k_Layout {.framePlanes = {0x80, 0x88, 0x90},
	                                                       .frameFlip = 0xA1,
	                                                       .planeWidth = 0x0,
	                                                       .planeHeight = 0x4,
	                                                       .planePitch = 0x10,
	                                                       .planeData = 0x8,
	                                                       .rendererWidth = 0x8C,
	                                                       .rendererHeight = 0x90,
	                                                       .rendererChannel = 0xA0};
	static constexpr game_layout::ChannelLayout k_ChannelLayout {
	    .componentChannel = 0x20, .messageObjectId = 0x28, .channelClipKey = 0xA0};
	/// The bytes of guard after each plane.
	static constexpr std::size_t k_GuardBytes = 4096;

	FakeGame(std::uint32_t width, std::uint32_t height, std::uint32_t padding)
	    : _renderer(0x200)
	    , _channel(0x200)
	    , _frame(0x200)
	    , _message(0x100)
	    , _component(0x100)
	{
		Put(_renderer, k_Layout.rendererChannel, Address(_channel));
		Put(_renderer, k_Layout.rendererWidth, width);
		Put(_renderer, k_Layout.rendererHeight, height);
		Put(_message, k_ChannelLayout.messageObjectId, k_Tv);
		Put(_component, k_ChannelLayout.componentChannel, Address(_channel));
		for (std::size_t index = 0; index < 3; ++index)
		{
			const auto w = index == 0 ? width : (width + 1) / 2;
			const auto h = index == 0 ? height : (height + 1) / 2;
			const std::uint64_t pitch = w + padding;
			auto& plane = _planes[index];
			plane.object.resize(0x40);
			plane.width = w;
			plane.height = h;
			plane.pitch = pitch;
			plane.data.assign(static_cast<std::size_t>(pitch * h) + k_GuardBytes, k_Guard);
			Put(plane.object, k_Layout.planeWidth, w);
			Put(plane.object, k_Layout.planeHeight, h);
			Put(plane.object, k_Layout.planePitch, pitch);
			Put(plane.object, k_Layout.planeData, Address(plane.data));
			Put(_frame, k_Layout.framePlanes[index], Address(plane.object));
		}
	}

	[[nodiscard]] std::uintptr_t Renderer() const { return Address(_renderer); }
	[[nodiscard]] std::uintptr_t Frame() const { return Address(_frame); }
	[[nodiscard]] std::uintptr_t Message() const { return Address(_message); }
	[[nodiscard]] std::uintptr_t Component() const { return Address(_component); }

	struct Plane
	{
		std::vector<std::byte> object;
		std::vector<std::uint8_t> data;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint64_t pitch = 0;

		[[nodiscard]] std::uint8_t At(std::uint32_t column, std::uint32_t row) const
		{
			return data[static_cast<std::size_t>(row * pitch + column)];
		}
	};

	[[nodiscard]] const Plane& PlaneAt(std::size_t index) const { return _planes[index]; }

	/// True if no byte in the padding of a row or after the rows changed.
	[[nodiscard]] bool GuardsKept() const
	{
		for (const auto& plane : _planes)
		{
			for (std::size_t index = 0; index < plane.data.size(); ++index)
			{
				const auto row = index / plane.pitch;
				const auto column = index % plane.pitch;
				if ((row >= plane.height || column >= plane.width) && plane.data[index] != k_Guard)
				{
					return false;
				}
			}
		}
		return true;
	}

private:
	template <typename T>
	static std::uintptr_t Address(const std::vector<T>& bytes)
	{
		return reinterpret_cast<std::uintptr_t>(bytes.data());
	}

	template <typename T>
	static void Put(std::vector<std::byte>& object, std::size_t offset, const T& value)
	{
		std::memcpy(object.data() + offset, &value, sizeof(T));
	}

	std::vector<std::byte> _renderer;
	std::vector<std::byte> _channel;
	std::vector<std::byte> _frame;
	std::vector<std::byte> _message;
	std::vector<std::byte> _component;
	std::array<Plane, 3> _planes;
};

/// Pump the runtime and call the callback of the frames until `done` is true or the time ends.
bool RunFrames(TvRuntime& runtime, const FakeGame& game, std::chrono::seconds timeout, const std::function<bool()>& done)
{
	const auto end = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < end)
	{
		runtime.OnRenderFrame(game.Renderer(), game.Frame());
		if (done())
		{
			return true;
		}
		std::this_thread::sleep_for(33ms);
	}
	return false;
}

/// Compare the planes of the fake frame with the front frame of the helper. The sizes are the same.
[[nodiscard]] bool SameAsHelper(const FakeGame& game, host::FrameBuffers& frames)
{
	const auto planes = frames.FrontPlanes();
	const std::array<std::span<const std::uint8_t>, 3> sources {planes.y, planes.u, planes.v};
	const std::array<std::uint32_t, 3> pitches {planes.pitchY, planes.pitchUV, planes.pitchUV};
	for (std::size_t index = 0; index < 3; ++index)
	{
		const auto& plane = game.PlaneAt(index);
		for (std::uint32_t row = 0; row < plane.height; ++row)
		{
			if (std::memcmp(&plane.data[static_cast<std::size_t>(row * plane.pitch)], &sources[index][row * pitches[index]],
			                plane.width) != 0)
			{
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] double MeanY(const FakeGame& game, std::uint32_t firstRow, std::uint32_t rows)
{
	const auto& plane = game.PlaneAt(0);
	double sum = 0.0;
	for (std::uint32_t row = firstRow; row < firstRow + rows; ++row)
	{
		for (std::uint32_t column = 0; column < plane.width; ++column)
		{
			sum += plane.At(column, row);
		}
	}
	return sum / (static_cast<double>(plane.width) * rows);
}

int HelperTest(Report& report, const Options& options)
{
	const auto helper = FindHelper(options);
	if (!std::filesystem::exists(helper))
	{
		Print("The helper is not there: " + helper.string());
		return k_Skip;
	}
	Print("The helper: " + helper.string());
	YoutubeTvSettings settings;
	settings.helperPath = helper.string();
	settings.width = 640;
	settings.height = 368;
	TvRuntime runtime(
	    settings, [](Ts4LogLevel, const std::string& text) { Print("  log: " + text); }, std::make_unique<ProcessMemory>());
	runtime.SetRendererLayout(FakeGame::k_Layout);
	runtime.SetChannelLayout(FakeGame::k_ChannelLayout);

	// The same size as the frames of the helper, with padding in each row.
	FakeGame same(640, 368, 64);
	// The game links the TV to its channel when it applies a list of clips.
	runtime.OnPlaylistMessage(same.Message());
	runtime.OnSetPlaylist(same.Component());
	runtime.OnPlaylistMessageDone();

	const auto played = runtime.Play(k_Tv, options.video);
	report.Check("play", played.HasValue(), played ? "" : played.GetError().message);
	if (!played)
	{
		return 1;
	}
	const auto playing = RunFrames(runtime, same, 90s, [&] {
		const auto* session = runtime.SessionOf(k_Tv);
		return session != nullptr && session->state == SessionState::Playing && session->framesWritten > 0;
	});
	report.Check("the video plays and the runtime writes frames", playing, runtime.Status()["videos"].dump());
	if (!playing)
	{
		return 1;
	}
	// Let the video play, so that the frame shows the video and not the start of the player.
	RunFrames(runtime, same, std::chrono::seconds(options.seconds), [] { return false; });
	auto video = runtime.VideoOf(k_Tv);
	report.Check("the shared memory of the video", video != nullptr);
	if (!video)
	{
		return 1;
	}
	runtime.OnRenderFrame(same.Renderer(), same.Frame());
	report.Check("the frame in the fake planes is the frame of the helper", SameAsHelper(same, video->Frames()),
	             std::format("frame {}, mean Y {:.1f}", video->Frames().FrontFrameNumber(), MeanY(same, 0, 368)));
	report.Check("no write outside the planes of the same size", same.GuardsKept());

	// A different size: the runtime scales the frame and makes black bars.
	FakeGame other(480, 360, 16);
	runtime.OnPlaylistMessage(other.Message());
	runtime.OnSetPlaylist(other.Component());
	runtime.OnPlaylistMessageDone();
	const auto before = runtime.SessionOf(k_Tv)->framesWritten;
	runtime.OnRenderFrame(other.Renderer(), other.Frame());
	const auto rect = FitImage(640, 368, 480, 360);
	report.Check("the runtime writes into a frame of a different size", runtime.SessionOf(k_Tv)->framesWritten == before + 1,
	             std::format("image {}x{} at row {}", rect.width, rect.height, rect.y));
	report.Check("the bars are black", other.PlaneAt(0).At(10, 0) == 16 && other.PlaneAt(1).At(5, 0) == 128,
	             std::format("Y {} U {}", other.PlaneAt(0).At(10, 0), other.PlaneAt(1).At(5, 0)));
	report.Check("the image is in the middle", std::abs(MeanY(other, rect.y, rect.height) - MeanY(same, 0, 368)) < 8.0,
	             std::format("mean Y {:.1f}", MeanY(other, rect.y, rect.height)));
	report.Check("no write outside the planes of the other size", other.GuardsKept());
	// Link the TV to its first channel again.
	runtime.OnPlaylistMessage(same.Message());
	runtime.OnSetPlaylist(same.Component());
	runtime.OnPlaylistMessageDone();

	// The speed of the game.
	runtime.SetSpeed(ClockSpeedMode::Paused, false);
	const auto paused = RunFrames(runtime, same, 10s, [&] { return runtime.SessionOf(k_Tv)->state == SessionState::Paused; });
	report.Check("paused at the speed 0", paused);
	runtime.SetSpeed(ClockSpeedMode::Speed3, false);
	const auto again = RunFrames(runtime, same, 15s, [&] { return runtime.SessionOf(k_Tv)->state == SessionState::Playing; });
	report.Check("plays again at the speed 3 (rate 2)", again);
	runtime.SetSpeed(ClockSpeedMode::SuperSpeed3, true);
	const auto super = RunFrames(runtime, same, 10s, [&] { return runtime.SessionOf(k_Tv)->state == SessionState::Paused; });
	const auto held = runtime.SessionOf(k_Tv)->framesWritten;
	RunFrames(runtime, same, 1s, [] { return false; });
	report.Check("paused at the super speed, and the last frame stays on the screen",
	             super && runtime.SessionOf(k_Tv)->framesWritten > held);
	runtime.SetSpeed(ClockSpeedMode::Normal, false);

	// The audio of the video from the left, then from the right.
	if (auto* audio = runtime.Audio())
	{
		runtime.SetListenerPose(k_Listener);
		for (const auto [name, x] : {std::pair {"left", -2.0F}, std::pair {"right", 2.0F}})
		{
			// The default axes are right-handed: the runtime changes the sign of z.
			runtime.SetTransform(k_Tv, ObjectTransform {.position = {x, 0.0F, -2.0F}, .orientation = {0.0F, 1.0F, 0.0F, 0.0F}});
			Print(std::format("  the video plays from the {} for 1 s", name));
			RunFrames(runtime, same, 1s, [] { return false; });
		}
		const auto status = audio->Status();
		Print("  audio: " + status.dump());
		const auto& voices = status["voices"];
		const bool fed = !voices.empty() && voices[0]["submittedBlocks"].get<std::uint64_t>() > 150;
		report.Check("the voice of the video got audio from the ring",
		             fed && voices[0]["silentBlocks"].get<std::uint64_t>() < voices[0]["submittedBlocks"].get<std::uint64_t>(),
		             voices.empty() ? "no voice" : voices[0].dump());
	}
	else
	{
		Print("  no audio engine: the audio test of the video is skipped");
	}

	report.Check("stop", runtime.Stop(k_Tv));
	Print("  status: " + runtime.Status().dump());
	return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	Options options;
	for (int index = 1; index < argc; ++index)
	{
		const std::wstring name = argv[index];
		const bool hasValue = index + 1 < argc;
		if (name == L"--tone-only")
		{
			options.toneOnly = true;
		}
		else if (name == L"--helper" && hasValue)
		{
			options.helper = argv[++index];
		}
		else if (name == L"--video" && hasValue)
		{
			options.video = std::filesystem::path(argv[++index]).string();
		}
		else if (name == L"--seconds" && hasValue)
		{
			options.seconds = std::stoi(argv[++index]);
		}
		else
		{
			std::fprintf(stderr, "unknown option %ls\n", name.c_str());
			return 2;
		}
	}
	Report report;
	Print("== The 3D audio with a test tone");
	const auto tone = ToneTest(report);
	auto result = tone;
	if (!options.toneOnly)
	{
		Print("== The helper, the frames and the audio of a video");
		result = std::max(result, HelperTest(report, options));
	}
	if (report.failed > 0)
	{
		Print(std::format("{} checks failed", report.failed));
		return 1;
	}
	Print(result == k_Skip ? "Some tests were skipped" : "All checks passed");
	return result;
}
