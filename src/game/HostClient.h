/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <common/Result.h>
#include <core/HostCommand.h>
#include <core/HostProtocol.h>

#include "game/WinHandle.h"

namespace ts4_mod::youtube_tv
{

/// The shared memory of one video of the helper. The view stays mapped while a copy of the pointer exists.
class MappedVideo
{
public:
	/// Map the shared memory from the handle of the event `opened`. The object owns the handle.
	[[nodiscard]] static ts4::Result<std::shared_ptr<MappedVideo>> Map(std::uintptr_t handle, std::size_t bytes);

	/// The frames. Only one thread reads them (the main thread of the game).
	[[nodiscard]] host::FrameBuffers& Frames() { return _block->Frames(); }
	/// The audio ring. Only one thread reads it (the thread of the audio).
	[[nodiscard]] AudioRing& Audio() { return _block->Audio(); }

private:
	MappedVideo() = default;

	UniqueHandle _mapping;
	UniqueView _view;
	std::optional<host::SharedBlock> _block;
};

/// The helper `youtube_tv_host.exe` as a child process. The client starts the helper in a job object that stops it
/// when the client closes. A thread reads the events of the helper; the main thread takes them with `TakeEvents`.
class HostClient
{
public:
	struct Options
	{
		std::filesystem::path exe;
		/// The cache folder of CEF, or empty for the default of the helper.
		std::filesystem::path cacheDir;
	};

	/// The default exe of the helper: `%LOCALAPPDATA%\Sims4Mods\youtube_tv\youtube_tv_host.exe`.
	[[nodiscard]] static ts4::Result<std::filesystem::path> DefaultExe();

	/// Start the helper. The result is an error if the exe is not there or the process cannot start.
	[[nodiscard]] static ts4::Result<std::unique_ptr<HostClient>> Start(const Options& options);

	/// Send `quit`, wait a short time for the helper, then stop each process of the job.
	~HostClient();
	HostClient(const HostClient&) = delete;
	HostClient& operator=(const HostClient&) = delete;

	/// Send one command. Return false if the pipe is closed.
	bool Send(const host::HostCommand& command);

	/// The events since the last call, in order. The client adds the event `exit` when the pipe of the events closes,
	/// and the event `mapError` if it cannot map the shared memory of an `opened` event.
	[[nodiscard]] std::vector<host::HostEvent> TakeEvents();

	/// The shared memory of a video, after its `opened` event, or null.
	[[nodiscard]] std::shared_ptr<MappedVideo> Video(std::int32_t id);
	/// Release the shared memory of a video. Copies of the pointer keep the view until they go.
	void Release(std::int32_t id);

	/// True while the pipe of the events is open.
	[[nodiscard]] bool Running() const;
	[[nodiscard]] DWORD ProcessId() const { return _processId; }

private:
	HostClient() = default;

	void ReadEvents();
	void Push(host::HostEvent event);

	UniqueHandle _job;
	UniqueHandle _process;
	UniqueHandle _input;
	UniqueHandle _output;
	DWORD _processId = 0;

	mutable std::mutex _mutex;
	std::vector<host::HostEvent> _events;
	std::map<std::int32_t, std::shared_ptr<MappedVideo>> _videos;
	bool _running = true;
	std::mutex _sendMutex;
	std::jthread _reader;
};

} // namespace ts4_mod::youtube_tv
