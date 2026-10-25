/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "HostClient.h"

#include <array>
#include <format>
#include <utility>

namespace ts4_mod::youtube_tv
{

namespace
{

/// The time that the client waits for the helper to stop after `quit`.
constexpr DWORD k_QuitWaitMs = 3000;
/// A line of the helper longer than this is not an event.
constexpr std::size_t k_MaxLineBytes = 1 << 20;

[[nodiscard]] host::HostEvent ClientEvent(std::string type, nlohmann::json data = nlohmann::json::object(), std::int32_t id = 0)
{
	data["event"] = type;
	if (id != 0)
	{
		data["id"] = id;
	}
	return host::HostEvent {.type = std::move(type), .id = id, .data = std::move(data)};
}

} // namespace

ts4::Result<std::shared_ptr<MappedVideo>> MappedVideo::Map(std::uintptr_t handle, std::size_t bytes)
{
	auto video = std::shared_ptr<MappedVideo>(new MappedVideo());
	video->_mapping.reset(reinterpret_cast<HANDLE>(handle));
	if (handle == 0 || bytes < sizeof(host::SharedBlockHeader))
	{
		return ts4::MakeError("the event has no shared memory");
	}
	video->_view.reset(::MapViewOfFile(video->_mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, bytes));
	if (!video->_view)
	{
		return ts4::MakeError("MapViewOfFile failed with the error {}", ::GetLastError());
	}
	// The size of the view is the size of the mapping, rounded up to a page. Read the real size before the use.
	MEMORY_BASIC_INFORMATION info {};
	if (::VirtualQuery(video->_view.get(), &info, sizeof(info)) == 0 || info.RegionSize < bytes)
	{
		return ts4::MakeError("the shared memory is smaller than {} bytes", bytes);
	}
	auto block = host::SharedBlock::Attach({static_cast<std::byte*>(video->_view.get()), bytes});
	if (!block)
	{
		return block.GetError();
	}
	video->_block = *block;
	return video;
}

ts4::Result<std::filesystem::path> HostClient::DefaultExe()
{
	std::wstring folder(MAX_PATH, L'\0');
	auto length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), static_cast<DWORD>(folder.size()));
	if (length > folder.size())
	{
		folder.resize(length);
		length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), static_cast<DWORD>(folder.size()));
	}
	if (length == 0 || length > folder.size())
	{
		return ts4::MakeError("the variable LOCALAPPDATA is not set");
	}
	folder.resize(length);
	return std::filesystem::path(folder) / L"Sims4Mods" / L"youtube_tv" / L"youtube_tv_host.exe";
}

ts4::Result<std::unique_ptr<HostClient>> HostClient::Start(const Options& options)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(options.exe, error))
	{
		return ts4::MakeError("the helper {} is not there", options.exe.string());
	}
	auto client = std::unique_ptr<HostClient>(new HostClient());
	client->_job.reset(::CreateJobObjectW(nullptr, nullptr));
	if (!client->_job)
	{
		return ts4::MakeError("CreateJobObjectW failed with the error {}", ::GetLastError());
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!::SetInformationJobObject(client->_job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
	{
		return ts4::MakeError("SetInformationJobObject failed with the error {}", ::GetLastError());
	}

	SECURITY_ATTRIBUTES inherit {
	    .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
	HANDLE childInput = nullptr;
	HANDLE parentInput = nullptr;
	HANDLE parentOutput = nullptr;
	HANDLE childOutput = nullptr;
	if (!::CreatePipe(&childInput, &parentInput, &inherit, 0))
	{
		return ts4::MakeError("CreatePipe failed with the error {}", ::GetLastError());
	}
	UniqueHandle childInputOwner(childInput);
	client->_input.reset(parentInput);
	if (!::CreatePipe(&parentOutput, &childOutput, &inherit, 0))
	{
		return ts4::MakeError("CreatePipe failed with the error {}", ::GetLastError());
	}
	UniqueHandle childOutputOwner(childOutput);
	client->_output.reset(parentOutput);
	::SetHandleInformation(parentInput, HANDLE_FLAG_INHERIT, 0);
	::SetHandleInformation(parentOutput, HANDLE_FLAG_INHERIT, 0);
	UniqueHandle nul(
	    ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr));

	// Give the child only its own ends of the pipes and NUL, not the other inheritable handles of the game.
	std::array<HANDLE, 3> inherited {childInput, childOutput, nul.get()};
	SIZE_T attributeBytes = 0;
	::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
	std::vector<std::byte> attributeBuffer(attributeBytes);
	auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
	if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
	{
		return ts4::MakeError("InitializeProcThreadAttributeList failed with the error {}", ::GetLastError());
	}
	struct AttributeListGuard
	{
		LPPROC_THREAD_ATTRIBUTE_LIST list;
		~AttributeListGuard() { ::DeleteProcThreadAttributeList(list); }
	} attributeGuard {attributes};
	if (!::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
	                                 sizeof(HANDLE) * inherited.size(), nullptr, nullptr))
	{
		return ts4::MakeError("UpdateProcThreadAttribute failed with the error {}", ::GetLastError());
	}

	STARTUPINFOEXW startup {};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = childInput;
	startup.StartupInfo.hStdOutput = childOutput;
	startup.StartupInfo.hStdError = nul.get();
	startup.lpAttributeList = attributes;
	auto commandLine = std::format(L"\"{}\" --parent-pid={}", options.exe.wstring(), ::GetCurrentProcessId());
	if (!options.cacheDir.empty())
	{
		commandLine += std::format(L" --cache-dir=\"{}\"", options.cacheDir.wstring());
	}
	PROCESS_INFORMATION process {};
	if (!::CreateProcessW(options.exe.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
	                      CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
	                      options.exe.parent_path().c_str(), &startup.StartupInfo, &process))
	{
		return ts4::MakeError("CreateProcessW failed with the error {}", ::GetLastError());
	}
	UniqueHandle thread(process.hThread);
	client->_process.reset(process.hProcess);
	client->_processId = process.dwProcessId;
	if (!::AssignProcessToJobObject(client->_job.get(), process.hProcess))
	{
		const auto code = ::GetLastError();
		::TerminateProcess(process.hProcess, 1);
		return ts4::MakeError("AssignProcessToJobObject failed with the error {}", code);
	}
	::ResumeThread(process.hThread);
	client->_reader = std::jthread([raw = client.get()] { raw->ReadEvents(); });
	client->_writer = std::jthread([raw = client.get()] { raw->WriteCommands(); });
	return client;
}

HostClient::~HostClient()
{
	Send(host::HostCommand {.type = host::CommandType::Quit});
	_commands.Close();
	if (_process)
	{
		::WaitForSingleObject(_process.get(), k_QuitWaitMs);
	}
	// Closing the job stops each process of the helper that is still there. Then a write to the pipe fails, so the
	// writer thread stops, and the pipe of the events closes, so the reader thread stops.
	_job.reset();
	if (_writer.joinable())
	{
		_writer.join();
	}
	_input.reset();
	if (_reader.joinable())
	{
		_reader.join();
	}
}

bool HostClient::Send(const host::HostCommand& command)
{
	return _commands.Push(host::CommandLine(command) + "\n");
}

void HostClient::WriteCommands()
{
	while (auto line = _commands.Pop())
	{
		DWORD written = 0;
		if (::WriteFile(_input.get(), line->data(), static_cast<DWORD>(line->size()), &written, nullptr) == 0 ||
		    written != line->size())
		{
			// The pipe closed: the helper stopped. Refuse the next commands.
			_commands.Close();
			return;
		}
	}
}

std::vector<host::HostEvent> HostClient::TakeEvents()
{
	const std::lock_guard lock(_mutex);
	return std::exchange(_events, {});
}

std::shared_ptr<MappedVideo> HostClient::Video(std::int32_t id)
{
	const std::lock_guard lock(_mutex);
	const auto it = _videos.find(id);
	return it == _videos.end() ? nullptr : it->second;
}

void HostClient::Release(std::int32_t id)
{
	const std::lock_guard lock(_mutex);
	_videos.erase(id);
}

bool HostClient::Running() const
{
	const std::lock_guard lock(_mutex);
	return _running;
}

void HostClient::Push(host::HostEvent event)
{
	const std::lock_guard lock(_mutex);
	_events.push_back(std::move(event));
}

void HostClient::ReadEvents()
{
	std::string buffer;
	std::array<char, 4096> chunk {};
	for (;;)
	{
		DWORD read = 0;
		if (!::ReadFile(_output.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr) || read == 0)
		{
			break;
		}
		buffer.append(chunk.data(), read);
		for (auto end = buffer.find('\n'); end != std::string::npos; end = buffer.find('\n'))
		{
			const auto line = buffer.substr(0, end);
			buffer.erase(0, end + 1);
			auto event = host::ParseHostEvent(line);
			if (!event)
			{
				continue;
			}
			if (event->type == "opened")
			{
				// Map the memory on this thread, so that the main thread finds the frames at once.
				const auto handle = event->data.value("sharedMemory", std::uint64_t {0});
				const auto bytes = event->data.value("sharedBytes", std::uint64_t {0});
				auto video = MappedVideo::Map(static_cast<std::uintptr_t>(handle), static_cast<std::size_t>(bytes));
				if (video)
				{
					const std::lock_guard lock(_mutex);
					_videos[event->id] = *video;
				}
				else
				{
					Push(ClientEvent("mapError", {{"message", video.GetError().message}}, event->id));
				}
			}
			Push(std::move(*event));
		}
		if (buffer.size() > k_MaxLineBytes)
		{
			buffer.clear();
		}
	}
	const std::lock_guard lock(_mutex);
	_running = false;
	_events.push_back(ClientEvent("exit"));
}

} // namespace ts4_mod::youtube_tv
