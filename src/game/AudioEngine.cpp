/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "AudioEngine.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <chrono>
#include <numbers>
#include <vector>

#include <windows.h>
#include <x3daudio.h>
#include <xaudio2.h>

#include "game/WinHandle.h"

namespace ts4_mod::youtube_tv
{

namespace
{

constexpr std::uint32_t k_SampleRate = 48000;
constexpr std::uint32_t k_Channels = 2;
/// One buffer has 10 ms of audio.
constexpr std::uint32_t k_BlockFrames = 480;
/// The voice keeps three buffers in its queue: 30 ms.
constexpr std::uint32_t k_QueuedBlocks = 3;
/// The voice has more buffers than its queue, so that the engine never writes a buffer that XAudio2 still reads.
constexpr std::uint32_t k_StoredBlocks = 8;
/// If the source has more audio than this, the engine removes the old part to keep the delay small.
constexpr std::size_t k_MaxDelayFrames = k_SampleRate / 5;
constexpr std::size_t k_KeepDelayFrames = k_SampleRate / 10;
/// The largest output that the engine supports: 7.1.
constexpr std::uint32_t k_MaxOutputChannels = 8;

using CreateFn = HRESULT(WINAPI*)(IXAudio2** xaudio, UINT32 flags, XAUDIO2_PROCESSOR processor);
using InitializeFn = HRESULT(__cdecl*)(UINT32 channelMask, FLOAT32 speedOfSound, BYTE* instance);
using CalculateFn = void(__cdecl*)(const BYTE* instance, const X3DAUDIO_LISTENER* listener, const X3DAUDIO_EMITTER* emitter,
                                   UINT32 flags, X3DAUDIO_DSP_SETTINGS* settings);

[[nodiscard]] X3DAUDIO_VECTOR ToX3d(Vec3 value)
{
	return {value.x, value.y, value.z};
}

} // namespace

/// The DLL of XAudio2, the engine and the mastering voice. The destructor releases them in the reverse order.
struct AudioEngine::Library
{
	UniqueLibrary module;
	IXAudio2* xaudio = nullptr;
	IXAudio2MasteringVoice* master = nullptr;
	std::array<BYTE, X3DAUDIO_HANDLE_BYTESIZE> x3d {};
	CalculateFn calculate = nullptr;

	~Library()
	{
		if (master != nullptr)
		{
			master->DestroyVoice();
		}
		if (xaudio != nullptr)
		{
			xaudio->StopEngine();
			xaudio->Release();
		}
	}
};

/// One source voice and its buffers.
struct AudioEngine::Voice
{
	IXAudio2SourceVoice* voice = nullptr;
	std::shared_ptr<PcmSource> source;
	std::vector<float> blocks = std::vector<float>(std::size_t {k_StoredBlocks} * k_BlockFrames * k_Channels);
	std::uint32_t nextBlock = 0;
	std::optional<AudioPose> emitter;
	float volume = 0.0F;
	std::uint64_t submitted = 0;
	std::uint64_t silentBlocks = 0;
	std::uint64_t droppedFrames = 0;

	~Voice()
	{
		if (voice != nullptr)
		{
			voice->DestroyVoice();
		}
	}
};

ts4::Result<std::unique_ptr<AudioEngine>> AudioEngine::Start(Settings settings)
{
	auto engine = std::unique_ptr<AudioEngine>(new AudioEngine());
	engine->_settings = settings;
	engine->_library = std::make_unique<Library>();
	auto& library = *engine->_library;
	library.module.reset(::LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
	if (!library.module)
	{
		return ts4::MakeError("the system has no xaudio2_9.dll (error {})", ::GetLastError());
	}
	const auto create = reinterpret_cast<CreateFn>(::GetProcAddress(library.module.get(), "XAudio2Create"));
	const auto initialize = reinterpret_cast<InitializeFn>(::GetProcAddress(library.module.get(), "X3DAudioInitialize"));
	library.calculate = reinterpret_cast<CalculateFn>(::GetProcAddress(library.module.get(), "X3DAudioCalculate"));
	if (create == nullptr || initialize == nullptr || library.calculate == nullptr)
	{
		return ts4::MakeError("xaudio2_9.dll has no XAudio2Create, X3DAudioInitialize or X3DAudioCalculate");
	}
	if (const auto result = create(&library.xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR); FAILED(result))
	{
		return ts4::MakeError("XAudio2Create failed with 0x{:08X}", static_cast<std::uint32_t>(result));
	}
	if (const auto result = library.xaudio->CreateMasteringVoice(&library.master); FAILED(result))
	{
		return ts4::MakeError("the mastering voice failed with 0x{:08X} (no output device?)",
		                      static_cast<std::uint32_t>(result));
	}
	DWORD mask = 0;
	library.master->GetChannelMask(&mask);
	XAUDIO2_VOICE_DETAILS details {};
	library.master->GetVoiceDetails(&details);
	engine->_outputChannels = std::clamp<std::uint32_t>(details.InputChannels, 1, k_MaxOutputChannels);
	if (const auto result = initialize(mask, X3DAUDIO_SPEED_OF_SOUND, library.x3d.data()); FAILED(result))
	{
		return ts4::MakeError("X3DAudioInitialize failed with 0x{:08X}", static_cast<std::uint32_t>(result));
	}
	engine->_feeder = std::jthread([raw = engine.get()](std::stop_token stop) { raw->Feed(stop); });
	return engine;
}

AudioEngine::~AudioEngine()
{
	_feeder = {};
	const std::lock_guard lock(_mutex);
	_voices.clear();
}

ts4::Result<void> AudioEngine::AddVoice(std::int32_t id, std::shared_ptr<PcmSource> source)
{
	auto voice = std::make_unique<Voice>();
	voice->source = std::move(source);
	WAVEFORMATEX format {};
	format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	format.nChannels = k_Channels;
	format.nSamplesPerSec = k_SampleRate;
	format.wBitsPerSample = 32;
	format.nBlockAlign = static_cast<WORD>(k_Channels * sizeof(float));
	format.nAvgBytesPerSec = k_SampleRate * format.nBlockAlign;
	if (const auto result = _library->xaudio->CreateSourceVoice(&voice->voice, &format); FAILED(result))
	{
		return ts4::MakeError("CreateSourceVoice failed with 0x{:08X}", static_cast<std::uint32_t>(result));
	}
	voice->voice->SetVolume(0.0F);
	voice->voice->Start();
	const std::lock_guard lock(_mutex);
	_voices[id] = std::move(voice);
	return {};
}

void AudioEngine::RemoveVoice(std::int32_t id)
{
	const std::lock_guard lock(_mutex);
	_voices.erase(id);
}

void AudioEngine::RemoveAll()
{
	const std::lock_guard lock(_mutex);
	_voices.clear();
}

bool AudioEngine::HasVoice(std::int32_t id) const
{
	const std::lock_guard lock(_mutex);
	return _voices.contains(id);
}

void AudioEngine::SetListener(const AudioPose& listener)
{
	const std::lock_guard lock(_mutex);
	_listener = listener;
}

void AudioEngine::SetVoice(std::int32_t id, const std::optional<AudioPose>& emitter, float volume)
{
	const std::lock_guard lock(_mutex);
	if (const auto it = _voices.find(id); it != _voices.end())
	{
		it->second->emitter = emitter;
		it->second->volume = volume;
	}
}

void AudioEngine::SetSettings(Settings settings)
{
	const std::lock_guard lock(_mutex);
	_settings = settings;
}

void AudioEngine::Update()
{
	const std::lock_guard lock(_mutex);
	X3DAUDIO_LISTENER listener {};
	listener.Position = ToX3d(_listener.position);
	listener.OrientFront = ToX3d(_listener.front);
	listener.OrientTop = ToX3d(_listener.top);
	// The viewer faces the screen. Thus the left channel of the video is on the right side of the front of the TV.
	std::array<FLOAT32, k_Channels> azimuths {std::numbers::pi_v<float> / 2.0F, 3.0F * std::numbers::pi_v<float> / 2.0F};
	std::array<FLOAT32, k_Channels * k_MaxOutputChannels> matrix {};
	for (auto& [id, voice] : _voices)
	{
		voice->voice->SetVolume(voice->volume);
		if (!voice->emitter)
		{
			// No position: the left channel to the left output and the right channel to the right output.
			matrix.fill(0.0F);
			matrix[0] = 1.0F;
			if (_outputChannels > 1)
			{
				matrix[1 * _outputChannels + 1] = 1.0F;
			}
			else
			{
				matrix[1] = 1.0F;
			}
			voice->voice->SetOutputMatrix(_library->master, k_Channels, _outputChannels, matrix.data());
			continue;
		}
		X3DAUDIO_EMITTER emitter {};
		emitter.Position = ToX3d(voice->emitter->position);
		emitter.OrientFront = ToX3d(voice->emitter->front);
		emitter.OrientTop = ToX3d(voice->emitter->top);
		emitter.ChannelCount = k_Channels;
		emitter.ChannelRadius = 0.3F;
		emitter.pChannelAzimuths = azimuths.data();
		emitter.InnerRadius = 0.5F;
		emitter.InnerRadiusAngle = X3DAUDIO_PI / 4.0F;
		emitter.CurveDistanceScaler = _settings.curveDistanceScaler;
		emitter.DopplerScaler = 0.0F;
		X3DAUDIO_DSP_SETTINGS dsp {};
		dsp.SrcChannelCount = k_Channels;
		dsp.DstChannelCount = _outputChannels;
		dsp.pMatrixCoefficients = matrix.data();
		_library->calculate(_library->x3d.data(), &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);
		voice->voice->SetOutputMatrix(_library->master, k_Channels, _outputChannels, matrix.data());
	}
}

nlohmann::json AudioEngine::Status() const
{
	const std::lock_guard lock(_mutex);
	nlohmann::json voices = nlohmann::json::array();
	for (const auto& [id, voice] : _voices)
	{
		voices.push_back({{"id", id},
		                  {"volume", voice->volume},
		                  {"positioned", voice->emitter.has_value()},
		                  {"submittedBlocks", voice->submitted},
		                  {"silentBlocks", voice->silentBlocks},
		                  {"droppedFrames", voice->droppedFrames}});
	}
	return {{"outputChannels", _outputChannels}, {"voices", std::move(voices)}};
}

void AudioEngine::Feed(std::stop_token stop)
{
	using namespace std::chrono_literals;
	while (!stop.stop_requested())
	{
		{
			const std::lock_guard lock(_mutex);
			for (auto& [id, voice] : _voices)
			{
				XAUDIO2_VOICE_STATE state {};
				voice->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
				auto& source = *voice->source;
				if (source.Available() > k_MaxDelayFrames)
				{
					const auto before = source.Available();
					source.DropOldest(k_KeepDelayFrames);
					voice->droppedFrames += before - source.Available();
				}
				for (auto queued = state.BuffersQueued; queued < k_QueuedBlocks; ++queued)
				{
					const auto blockSamples = std::size_t {k_BlockFrames} * k_Channels;
					const std::span block(voice->blocks.data() + voice->nextBlock * blockSamples, blockSamples);
					const auto read = source.Read(block);
					// A source with too few frames gives silence for the rest of the block.
					std::fill(block.begin() + static_cast<std::ptrdiff_t>(read * k_Channels), block.end(), 0.0F);
					if (read == 0)
					{
						++voice->silentBlocks;
					}
					XAUDIO2_BUFFER buffer {};
					buffer.AudioBytes = static_cast<UINT32>(blockSamples * sizeof(float));
					buffer.pAudioData = reinterpret_cast<const BYTE*>(block.data());
					if (FAILED(voice->voice->SubmitSourceBuffer(&buffer)))
					{
						break;
					}
					++voice->submitted;
					voice->nextBlock = (voice->nextBlock + 1) % k_StoredBlocks;
				}
			}
		}
		std::this_thread::sleep_for(5ms);
	}
}

} // namespace ts4_mod::youtube_tv
