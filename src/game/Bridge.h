/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstdint>

// The C interface of the bridge for the script mod (version 2). `Bridge.cpp` has the code. The Python module of the
// mod calls the same functions.

extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvApiVersion();
extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvPlay(std::uint64_t objectId, const char* url,
                                                                     const float* position3, const float* orientation4);
extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvStop(std::uint64_t objectId);
extern "C" __declspec(dllexport) void __cdecl YoutubeTvStopAll();
extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetSpeed(std::uint32_t clockSpeedMode, double speedScale,
                                                                std::uint32_t superSpeed);
extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetTransform(std::uint64_t objectId, const float* position3,
                                                                    const float* orientation4);
extern "C" __declspec(dllexport) void __cdecl YoutubeTvSetScreenKey(std::uint64_t objectId, std::uint64_t instance,
                                                                    std::uint32_t type, std::uint32_t group);
extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvSetPlaylistClips(std::uint64_t instance, std::uint32_t type,
                                                                                 std::uint32_t group,
                                                                                 const std::uint8_t* clipKeys,
                                                                                 std::uint32_t size);
extern "C" __declspec(dllexport) std::uint32_t __cdecl YoutubeTvLastError(char* buffer, std::uint32_t size);
