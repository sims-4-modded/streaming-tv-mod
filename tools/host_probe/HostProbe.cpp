/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

// The test client of the helper youtube_tv_host.exe. It does what the DLL mod will do:
// 1. It starts the helper in a job object, so that no process of the helper stays after the probe.
// 2. It opens one video, and reads the I420 frames and the PCM audio from the shared memory.
// 3. It saves one frame, changed back from I420 to BGRA, as a PNG file.
// 4. It tests pause, play and setRate, and reads the events of the player.
// 5. It finds the audio sessions of the helper processes: CEF must not play the sound itself.
// 6. It sends quit, and checks that all processes of the helper stopped.
//
// Usage: host_probe [--host <exe>] [--video <id or URL>] [--width <n>] [--height <n>] [--frames <n>] [--png <file>]
//                   [--log <file>] [--matrix bt601|bt709] [--range limited|full]

#include <cmath>
#include <condition_variable>
#include <cstdio>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <audiopolicy.h>
#include <core/HostCommand.h>
#include <core/HostProtocol.h>
#include <core/Yuv.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

namespace
{

using namespace ts4_mod::youtube_tv;
using namespace std::chrono_literals;
using Microsoft::WRL::ComPtr;

struct HandleCloser
{
	void operator()(HANDLE handle) const
	{
		if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(handle);
		}
	}
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

struct ViewUnmapper
{
	void operator()(void* view) const
	{
		if (view != nullptr)
		{
			::UnmapViewOfFile(view);
		}
	}
};
using UniqueView = std::unique_ptr<void, ViewUnmapper>;

/// Calls CoUninitialize at the end of the scope.
struct ComScope
{
	ComScope() { _ok = SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
	~ComScope()
	{
		if (_ok)
		{
			::CoUninitialize();
		}
	}
	ComScope(const ComScope&) = delete;
	ComScope& operator=(const ComScope&) = delete;

private:
	bool _ok = false;
};

struct Options
{
	std::filesystem::path host;
	std::string video = "M7lc1UVf-VE";
	std::uint32_t width = 1280;
	std::uint32_t height = 720;
	std::uint64_t frames = 60;
	std::filesystem::path png;
	std::filesystem::path log;
	std::string matrix = "bt601";
	std::string range = "limited";
};

[[nodiscard]] std::filesystem::path ExeFolder()
{
	std::wstring path(MAX_PATH, L'\0');
	const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
	path.resize(length);
	return std::filesystem::path(path).parent_path();
}

[[nodiscard]] std::string Today()
{
	SYSTEMTIME time {};
	::GetLocalTime(&time);
	return std::format("{:04}-{:02}-{:02}", time.wYear, time.wMonth, time.wDay);
}

[[nodiscard]] std::optional<Options> ReadOptions(int argc, wchar_t** argv)
{
	Options options;
	options.host = ExeFolder().parent_path() / L"host" / L"youtube_tv_host.exe";
	options.png = std::filesystem::path(L"D:/sims_4/screenshots") / std::format("cef-helper-{}.png", Today());
	options.log = std::filesystem::path(L"D:/sims_4/runs") / std::format("{}-cef-helper", Today()) / "probe-events.log";
	for (int index = 1; index + 1 < argc; index += 2)
	{
		const std::wstring name = argv[index];
		const std::filesystem::path value = argv[index + 1];
		if (name == L"--host")
		{
			options.host = value;
		}
		else if (name == L"--video")
		{
			options.video = value.string();
		}
		else if (name == L"--width")
		{
			options.width = static_cast<std::uint32_t>(std::stoul(value.string()));
		}
		else if (name == L"--height")
		{
			options.height = static_cast<std::uint32_t>(std::stoul(value.string()));
		}
		else if (name == L"--frames")
		{
			options.frames = std::stoull(value.string());
		}
		else if (name == L"--png")
		{
			options.png = value;
		}
		else if (name == L"--log")
		{
			options.log = value;
		}
		else if (name == L"--matrix")
		{
			options.matrix = value.string();
		}
		else if (name == L"--range")
		{
			options.range = value.string();
		}
		else
		{
			std::fprintf(stderr, "unknown option %ls\n", name.c_str());
			return std::nullopt;
		}
	}
	return options;
}

/// What the probe knows from the events of the helper. The reader thread writes it; the main thread waits on it.
struct ProbeState
{
	std::mutex mutex;
	std::condition_variable changed;
	bool hello = false;
	bool bye = false;
	bool ended = false; ///< The pipe of the events closed.
	std::optional<host::HostEvent> opened;
	std::optional<host::HostEvent> ready;
	std::string state;
	double time = -1.0;
	std::vector<double> rates;
	std::vector<std::string> errors;
	std::vector<std::string> states;
	std::string quality;
	nlohmann::json audio;
	nlohmann::json helloData;
	nlohmann::json lastTime;

	/// Wait until the predicate is true, or the time ends. Returns the value of the predicate.
	bool WaitFor(std::chrono::milliseconds timeout, const std::function<bool()>& predicate)
	{
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, timeout, predicate);
	}
};

void Print(const std::string& text)
{
	std::printf("%s\n", text.c_str());
	std::fflush(stdout);
}

/// Read the event lines of the helper until the pipe closes.
void ReadEvents(HANDLE pipe, ProbeState& state, std::ofstream& log)
{
	std::string buffer;
	std::array<char, 4096> chunk {};
	for (;;)
	{
		DWORD read = 0;
		if (!::ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) || read == 0)
		{
			break;
		}
		buffer.append(chunk.data(), read);
		for (auto end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n'))
		{
			const std::string line = buffer.substr(0, end);
			buffer.erase(0, end + 1);
			log << line << '\n';
			log.flush();
			auto event = host::ParseHostEvent(line);
			if (!event)
			{
				Print("  ! not an event: " + line);
				continue;
			}
			if (event->type != "time")
			{
				Print("  < " + line);
			}
			const std::lock_guard lock(state.mutex);
			const auto& data = event->data;
			if (event->type == "hello")
			{
				state.hello = true;
				state.helloData = data;
			}
			else if (event->type == "bye")
			{
				state.bye = true;
			}
			else if (event->type == "opened")
			{
				state.opened = *event;
			}
			else if (event->type == "ready")
			{
				state.ready = *event;
			}
			else if (event->type == "state")
			{
				state.state = data.value("state", "");
				state.states.push_back(state.state);
			}
			else if (event->type == "rate")
			{
				state.rates.push_back(data.value("rate", 0.0));
			}
			else if (event->type == "time")
			{
				state.time = data.value("time", -1.0);
				state.lastTime = data;
			}
			else if (event->type == "error")
			{
				state.errors.push_back(line);
			}
			else if (event->type == "quality")
			{
				state.quality = data.value("quality", "");
			}
			else if (event->type == "audio")
			{
				state.audio = data;
			}
			state.changed.notify_all();
		}
	}
	const std::lock_guard lock(state.mutex);
	state.ended = true;
	state.changed.notify_all();
}

/// The helper process and its pipes.
struct Helper
{
	UniqueHandle job;
	UniqueHandle process;
	UniqueHandle input;  ///< The probe writes commands here.
	UniqueHandle output; ///< The probe reads events here.

	bool Send(const host::HostCommand& command) const
	{
		const auto line = host::CommandLine(command) + "\n";
		Print("  > " + host::CommandLine(command));
		DWORD written = 0;
		return ::WriteFile(input.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr) != 0;
	}

	[[nodiscard]] std::vector<DWORD> Processes() const
	{
		std::vector<std::byte> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 256 * sizeof(ULONG_PTR));
		auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.data());
		if (!::QueryInformationJobObject(job.get(), JobObjectBasicProcessIdList, list, static_cast<DWORD>(buffer.size()),
		                                 nullptr))
		{
			return {};
		}
		std::vector<DWORD> ids;
		for (DWORD index = 0; index < list->NumberOfProcessIdsInList; ++index)
		{
			ids.push_back(static_cast<DWORD>(list->ProcessIdList[index]));
		}
		return ids;
	}
};

[[nodiscard]] std::optional<Helper> StartHelper(const Options& options)
{
	Helper helper;
	helper.job.reset(::CreateJobObjectW(nullptr, nullptr));
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	::SetInformationJobObject(helper.job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits));

	SECURITY_ATTRIBUTES inherit {
	    .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
	HANDLE childInput = nullptr;
	HANDLE probeInput = nullptr;
	HANDLE probeOutput = nullptr;
	HANDLE childOutput = nullptr;
	if (!::CreatePipe(&childInput, &probeInput, &inherit, 0) || !::CreatePipe(&probeOutput, &childOutput, &inherit, 0))
	{
		return std::nullopt;
	}
	UniqueHandle childInputOwner(childInput);
	UniqueHandle childOutputOwner(childOutput);
	helper.input.reset(probeInput);
	helper.output.reset(probeOutput);
	// Only the ends of the helper go to the helper.
	::SetHandleInformation(probeInput, HANDLE_FLAG_INHERIT, 0);
	::SetHandleInformation(probeOutput, HANDLE_FLAG_INHERIT, 0);
	UniqueHandle nul(
	    ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));

	STARTUPINFOW startup {.cb = sizeof(STARTUPINFOW)};
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = childInput;
	startup.hStdOutput = childOutput;
	startup.hStdError = nul.get();
	auto commandLine = std::format(L"\"{}\" --parent-pid={} --cache-dir=\"{}\"", options.host.wstring(),
	                               ::GetCurrentProcessId(), (options.log.parent_path() / L"cache").wstring());
	PROCESS_INFORMATION process {};
	if (!::CreateProcessW(options.host.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
	                      nullptr, options.host.parent_path().c_str(), &startup, &process))
	{
		Print(std::format("CreateProcessW failed: error {}", ::GetLastError()));
		return std::nullopt;
	}
	UniqueHandle thread(process.hThread);
	helper.process.reset(process.hProcess);
	::AssignProcessToJobObject(helper.job.get(), process.hProcess);
	::ResumeThread(process.hThread);
	return helper;
}

/// Save BGRA pixels as a PNG file with WIC.
[[nodiscard]] bool SavePng(const std::filesystem::path& path, const std::vector<std::uint8_t>& pixels, std::uint32_t width,
                           std::uint32_t height)
{
	ComPtr<IWICImagingFactory> factory;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	return SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
	       SUCCEEDED(factory->CreateStream(&stream)) &&
	       SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
	       SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
	       SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
	       SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
	       SUCCEEDED(frame->SetSize(width, height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
	       format == GUID_WICPixelFormat32bppBGRA &&
	       SUCCEEDED(
	           frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()))) &&
	       SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

/// The highest peak of the audio sessions of these processes on the default output device, in the time given. The
/// result is -1 if no session of these processes exists.
[[nodiscard]] float AudioSessionPeak(const std::set<DWORD>& processes, std::chrono::milliseconds duration, int& sessions)
{
	sessions = 0;
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDevice> device;
	ComPtr<IAudioSessionManager2> manager;
	ComPtr<IAudioSessionEnumerator> list;
	if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
	    FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
	    FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
	    FAILED(manager->GetSessionEnumerator(&list)))
	{
		return -2.0F;
	}
	std::vector<ComPtr<IAudioMeterInformation>> meters;
	int count = 0;
	list->GetCount(&count);
	for (int index = 0; index < count; ++index)
	{
		ComPtr<IAudioSessionControl> control;
		ComPtr<IAudioSessionControl2> control2;
		DWORD pid = 0;
		if (SUCCEEDED(list->GetSession(index, &control)) && SUCCEEDED(control.As(&control2)) &&
		    SUCCEEDED(control2->GetProcessId(&pid)) && processes.contains(pid))
		{
			ComPtr<IAudioMeterInformation> meter;
			if (SUCCEEDED(control.As(&meter)))
			{
				meters.push_back(meter);
			}
		}
	}
	sessions = static_cast<int>(meters.size());
	if (meters.empty())
	{
		return -1.0F;
	}
	float peak = 0.0F;
	const auto end = std::chrono::steady_clock::now() + duration;
	while (std::chrono::steady_clock::now() < end)
	{
		for (const auto& meter : meters)
		{
			float value = 0.0F;
			meter->GetPeakValue(&value);
			peak = std::max(peak, value);
		}
		std::this_thread::sleep_for(20ms);
	}
	return peak;
}

/// Read the audio ring in a thread, as the DLL mod will do, and keep the samples.
class AudioCollector
{
public:
	explicit AudioCollector(host::SharedBlock& block)
	    : _block(block)
	    , _thread([this](std::stop_token stop) { Run(stop); })
	{
	}

	/// The frames that the collector read.
	[[nodiscard]] std::size_t Frames()
	{
		const std::lock_guard lock(_mutex);
		return _samples.size() / host::k_AudioChannels;
	}

	/// The RMS and the peak of the last `frames` frames.
	[[nodiscard]] std::pair<double, double> Level(std::size_t frames)
	{
		const std::lock_guard lock(_mutex);
		const auto count = std::min(frames * host::k_AudioChannels, _samples.size());
		double sum = 0.0;
		double peak = 0.0;
		for (std::size_t index = _samples.size() - count; index < _samples.size(); ++index)
		{
			sum += double {_samples[index]} * _samples[index];
			peak = std::max(peak, std::abs(double {_samples[index]}));
		}
		return {count == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(count)), peak};
	}

private:
	void Run(std::stop_token stop)
	{
		std::vector<float> chunk(4800 * host::k_AudioChannels);
		while (!stop.stop_requested())
		{
			const auto read = _block.Audio().Read(chunk);
			if (read > 0)
			{
				const std::lock_guard lock(_mutex);
				_samples.insert(_samples.end(), chunk.begin(), chunk.begin() + read * host::k_AudioChannels);
			}
			std::this_thread::sleep_for(10ms);
		}
	}

	host::SharedBlock& _block;
	std::mutex _mutex;
	std::vector<float> _samples;
	std::jthread _thread;
};

/// The checks of the probe, with their results.
struct Report
{
	std::vector<std::pair<std::string, bool>> checks;
	nlohmann::json values = nlohmann::json::object();

	void Check(const std::string& name, bool passed, const std::string& detail = {})
	{
		checks.emplace_back(name, passed);
		Print(std::format("[{}] {}{}", passed ? "PASS" : "FAIL", name, detail.empty() ? "" : ": " + detail));
	}
};

int Run(const Options& options)
{
	Report report;
	if (!std::filesystem::exists(options.host))
	{
		Print("The helper does not exist: " + options.host.string());
		return 2;
	}
	std::filesystem::create_directories(options.log.parent_path());
	std::filesystem::create_directories(options.png.parent_path());
	std::ofstream log(options.log);

	auto helper = StartHelper(options);
	if (!helper)
	{
		Print("The probe cannot start the helper.");
		return 2;
	}
	ProbeState state;
	std::jthread reader([&] { ReadEvents(helper->output.get(), state, log); });
	const auto started = std::chrono::steady_clock::now();
	const auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };

	report.Check("hello", state.WaitFor(30s, [&] { return state.hello; }));

	host::HostCommand open {.type = host::CommandType::Open, .id = 1, .width = options.width, .height = options.height};
	{
		auto parsed = host::ParseHostCommand(nlohmann::json {
		    {"cmd", "open"},
		    {"id", 1},
		    {"url", options.video},
		    {"width", options.width},
		    {"height", options.height},
		    {"matrix", options.matrix},
		    {"range", options.range}}.dump());
		if (!parsed)
		{
			Print("The video or the size is not correct: " + parsed.GetError().message);
			return 2;
		}
		open = *parsed;
	}
	helper->Send(open);
	const bool opened =
	    state.WaitFor(15s, [&] { return state.opened.has_value() || !state.errors.empty(); }) && state.opened.has_value();
	report.Check("opened", opened);

	UniqueHandle mapping;
	UniqueView view;
	std::optional<host::SharedBlock> block;
	if (opened)
	{
		const auto handle = state.opened->data.value("sharedMemory", std::uint64_t {0});
		const auto bytes = state.opened->data.value("sharedBytes", std::size_t {0});
		mapping.reset(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle)));
		view.reset(::MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, bytes));
		if (view)
		{
			auto attached = host::SharedBlock::Attach({static_cast<std::byte*>(view.get()), bytes});
			if (attached)
			{
				block = *attached;
			}
			else
			{
				Print("Attach failed: " + attached.GetError().message);
			}
		}
	}
	report.Check("the parent maps the shared memory", block.has_value());
	if (!block)
	{
		helper->Send({.type = host::CommandType::Quit});
		::WaitForSingleObject(helper->process.get(), 15000);
		return 1;
	}
	AudioCollector audio(*block);

	report.Check("ready", state.WaitFor(60s, [&] { return state.ready.has_value() || !state.errors.empty(); }) &&
	                          state.ready.has_value());
	const bool playing =
	    state.WaitFor(90s, [&] { return state.state == "playing" || !state.errors.empty(); }) && state.state == "playing";
	report.Check("state playing", playing, std::format("after {:.1f} s", elapsed()));

	// Let the video play, so that the frame shows the video and not the start of the player.
	std::this_thread::sleep_for(8s);
	const auto frames = block->Frames().FrameCounter();
	report.Check(std::format("at least {} frames", options.frames), frames >= options.frames, std::format("{} frames", frames));
	report.values["frames"] = frames;

	// One frame from I420 back to BGRA, as PNG.
	{
		auto& buffers = block->Frames();
		buffers.TakeNewest();
		const auto planes = buffers.FrontPlanes();
		std::vector<std::uint8_t> bgra(std::size_t {planes.width} * planes.height * 4);
		const bool converted = ConvertI420ToBgra(planes, bgra, planes.width * 4, buffers.Matrix(), buffers.Range());
		double meanY = 0.0;
		for (const auto value : planes.y)
		{
			meanY += value;
		}
		meanY /= static_cast<double>(planes.y.size());
		std::set<std::uint8_t> levels(planes.y.begin(), planes.y.end());
		const bool saved = converted && SavePng(options.png, bgra, planes.width, planes.height);
		report.Check("the frame PNG", saved,
		             std::format("{} ({}x{}, frame {}, mean Y {:.1f}, {} Y levels)", options.png.string(), planes.width,
		                         planes.height, buffers.FrontFrameNumber(), meanY, levels.size()));
		report.Check("the frame is not empty", levels.size() > 16);
		report.values["png"] = options.png.string();
		report.values["meanY"] = meanY;
	}

	// The audio: at least one second, with sound.
	state.WaitFor(10s, [&] { return audio.Frames() >= host::k_AudioSampleRate * 2; });
	{
		const auto collected = audio.Frames();
		const auto [rms, peak] = audio.Level(collected);
		report.Check("at least 1 second of audio", collected >= host::k_AudioSampleRate,
		             std::format("{} frames ({:.2f} s)", collected, collected / double {host::k_AudioSampleRate}));
		report.Check("the audio RMS is more than 0", rms > 0.0, std::format("RMS {:.4f}, peak {:.4f}", rms, peak));
		report.values["audioFrames"] = collected;
		report.values["audioRms"] = rms;
		report.values["audioPeak"] = peak;
	}

	// CEF must not play the sound itself. In the same time window, the ring gets the sound, and the audio sessions of
	// the helper processes on the output device stay at least 40 dB lower.
	{
		const auto ids = helper->Processes();
		int sessions = 0;
		const auto framesBefore = audio.Frames();
		const float sessionPeak = AudioSessionPeak({ids.begin(), ids.end()}, 4s, sessions);
		const auto [ringRms, ringPeak] = audio.Level(audio.Frames() - framesBefore);
		const bool muted = ringPeak > 0.01 && sessionPeak < ringPeak * 0.01;
		report.Check("CEF plays no sound itself", muted,
		             std::format("{} helper processes, {} audio sessions, session peak {:.5f}, ring peak {:.4f} in the "
		                         "same 4 s",
		                         ids.size(), sessions, sessionPeak, ringPeak));
		report.values["helperProcesses"] = ids.size();
		report.values["helperAudioSessions"] = sessions;
		report.values["helperSessionPeak"] = sessionPeak;
		report.values["ringPeakInSameWindow"] = ringPeak;
	}

	// Pause: the state changes, and the time stops.
	helper->Send({.type = host::CommandType::Pause, .id = 1});
	const bool paused = state.WaitFor(10s, [&] { return state.state == "paused"; });
	report.Check("pause gives the state paused", paused);
	if (paused)
	{
		std::this_thread::sleep_for(1s);
		double before = 0.0;
		{
			const std::lock_guard lock(state.mutex);
			before = state.time;
		}
		const auto framesBefore = block->Frames().FrameCounter();
		std::this_thread::sleep_for(3s);
		double after = 0.0;
		{
			const std::lock_guard lock(state.mutex);
			after = state.time;
		}
		const auto framesAfter = block->Frames().FrameCounter();
		report.Check("the time stops while paused", std::abs(after - before) < 0.1,
		             std::format("{:.2f} s to {:.2f} s, {} new frames in 3 s", before, after, framesAfter - framesBefore));
	}

	helper->Send({.type = host::CommandType::Play, .id = 1});
	report.Check("play gives the state playing", state.WaitFor(10s, [&] { return state.state == "playing"; }));

	// The playback rate 2: the player tells the actual rate, and the time goes twice as fast.
	helper->Send({.type = host::CommandType::SetRate, .id = 1, .rate = 2.0});
	const bool rated = state.WaitFor(10s, [&] { return std::ranges::find(state.rates, 2.0) != state.rates.end(); });
	report.Check("setRate 2 gives the rate event 2", rated);
	if (rated)
	{
		std::this_thread::sleep_for(1s);
		double before = 0.0;
		{
			const std::lock_guard lock(state.mutex);
			before = state.time;
		}
		const auto wallBefore = std::chrono::steady_clock::now();
		std::this_thread::sleep_for(4s);
		double after = 0.0;
		{
			const std::lock_guard lock(state.mutex);
			after = state.time;
		}
		const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallBefore).count();
		const double speed = (after - before) / wall;
		report.Check("the time goes at the rate 2", speed > 1.6 && speed < 2.4,
		             std::format("{:.2f} s of video in {:.2f} s", after - before, wall));
		report.values["measuredRate"] = speed;
	}
	{
		const std::lock_guard lock(state.mutex);
		report.values["rateEvents"] = state.rates;
		report.values["states"] = state.states;
		report.values["errors"] = state.errors;
		report.values["quality"] = state.quality;
		report.values["lastTime"] = state.lastTime;
		report.values["hello"] = state.helloData;
		if (state.ready)
		{
			report.values["codecs"] = state.ready->data.value("codecs", nlohmann::json {});
			report.values["rates"] = state.ready->data.value("rates", nlohmann::json {});
		}
		report.values["audioEvent"] = state.audio;
	}

	// Quit: the helper closes the browser, sends bye and stops with all its processes.
	helper->Send({.type = host::CommandType::Quit});
	const bool bye = state.WaitFor(15s, [&] { return state.bye; });
	const bool stopped = ::WaitForSingleObject(helper->process.get(), 15000) == WAIT_OBJECT_0;
	DWORD exitCode = 0;
	::GetExitCodeProcess(helper->process.get(), &exitCode);
	std::this_thread::sleep_for(2s);
	const auto left = helper->Processes();
	report.Check("quit gives bye", bye);
	report.Check("the helper stops", stopped, std::format("exit code {}", exitCode));
	report.Check("no process of the helper stays", left.empty(), std::format("{} processes", left.size()));
	if (!left.empty())
	{
		::TerminateJobObject(helper->job.get(), 1);
	}
	helper->input.reset();
	reader.join();

	const auto failed = std::ranges::count_if(report.checks, [](const auto& check) { return !check.second; });
	Print(std::format("SUMMARY {} checks, {} failed", report.checks.size(), failed));
	Print("VALUES " + report.values.dump());
	return failed == 0 ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
	const auto options = ReadOptions(argc, argv);
	if (!options)
	{
		return 2;
	}
	const ComScope com;
	return Run(*options);
}
