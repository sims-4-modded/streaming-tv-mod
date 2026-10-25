/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "VideoLayout.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace ts4_mod::youtube_tv::game_layout
{

namespace
{

/// One field of an object: its offset and its size in bytes.
struct FieldSpan
{
	std::size_t offset = 0;
	std::size_t size = 0;
};

/// True if no two fields use the same byte.
[[nodiscard]] bool Separate(std::vector<FieldSpan> fields)
{
	std::ranges::sort(fields, {}, &FieldSpan::offset);
	for (std::size_t index = 1; index < fields.size(); ++index)
	{
		if (fields[index - 1].offset + fields[index - 1].size > fields[index].offset)
		{
			return false;
		}
	}
	return true;
}

/// Read one offset into `out`. Return the error of the rule, or nothing.
[[nodiscard]] std::optional<ts4::Error> Read(std::span<const std::byte> code, const DisplacementRule& rule, std::size_t& out)
{
	auto value = ReadFieldOffset(code, rule);
	if (!value)
	{
		return value.GetError();
	}
	out = *value;
	return std::nullopt;
}

} // namespace

ts4::Result<RendererLayout> ReadRendererLayout(std::span<const std::byte> renderFrameCode)
{
	RendererLayout layout;
	const std::pair<const DisplacementRule*, std::size_t*> rules[] = {
	    {&k_FramePlaneY, &layout.framePlanes[0]},      {&k_FramePlaneU, &layout.framePlanes[1]},
	    {&k_FramePlaneV, &layout.framePlanes[2]},      {&k_FrameFlip, &layout.frameFlip},
	    {&k_PlaneWidth, &layout.planeWidth},           {&k_PlaneHeight, &layout.planeHeight},
	    {&k_PlanePitch, &layout.planePitch},           {&k_PlaneData, &layout.planeData},
	    {&k_RendererWidth, &layout.rendererWidth},     {&k_RendererHeight, &layout.rendererHeight},
	    {&k_RendererChannel, &layout.rendererChannel},
	};
	for (const auto& [rule, out] : rules)
	{
		if (auto error = Read(renderFrameCode, *rule, *out))
		{
			return *error;
		}
	}
	// The frame has three pointers and a flag. The plane has two 32-bit sizes, a pointer and a 64-bit pitch.
	if (!Separate({{layout.framePlanes[0], 8}, {layout.framePlanes[1], 8}, {layout.framePlanes[2], 8}, {layout.frameFlip, 1}}))
	{
		return ts4::MakeError("the fields of the frame overlap");
	}
	if (!Separate({{layout.planeWidth, 4}, {layout.planeHeight, 4}, {layout.planePitch, 8}, {layout.planeData, 8}}))
	{
		return ts4::MakeError("the fields of the plane overlap");
	}
	if (!Separate({{layout.rendererWidth, 4}, {layout.rendererHeight, 4}, {layout.rendererChannel, 8}}))
	{
		return ts4::MakeError("the fields of the renderer overlap");
	}
	return layout;
}

ts4::Result<ChannelLayout> ReadChannelLayout(std::span<const std::byte> messageCode, std::uintptr_t messageCodeAddress,
                                             std::span<const std::byte> channelUpdateCode, const CodeAt& codeAt)
{
	ChannelLayout layout;
	if (auto error = Read(messageCode, k_MessageData, layout.messageData))
	{
		return *error;
	}
	if (auto error = Read(messageCode, k_MessageSize, layout.messageSize))
	{
		return *error;
	}
	if (layout.messageData == layout.messageSize)
	{
		return ts4::MakeError("the pointer and the size of the bytes of a message have the same offset");
	}
	std::size_t objectKey = 0;
	if (auto error = Read(messageCode, k_MessageObjectId, objectKey))
	{
		return *error;
	}
	auto lookup = ReadRelativeTarget(messageCode, messageCodeAddress, k_ObjectLookupCall);
	if (!lookup)
	{
		return lookup.GetError();
	}
	const auto lookupCode = codeAt(*lookup, k_ObjectLookupWindow);
	if (lookupCode.empty())
	{
		return ts4::MakeError("the function that finds the object of a playlist is not in the code of the game");
	}
	std::size_t idInKey = 0;
	if (auto error = Read(lookupCode, k_ObjectIdInKey, idInKey))
	{
		return *error;
	}
	layout.messageObjectId = objectKey + idInKey;
	if (auto error = Read(channelUpdateCode, k_ChannelClipKey, layout.channelClipKey))
	{
		return *error;
	}
	return layout;
}

ts4::Result<std::size_t> ReadListenerLayout(std::span<const std::byte> listenerCode)
{
	return ReadFieldOffset(listenerCode, k_ListenerCameraMatrix);
}

ts4::Result<VideoSettingsLayout> ReadVideoSettingsLayout(std::span<const std::byte> renderFrameCode, std::uintptr_t codeAddress,
                                                         const CodeAt& codeAt)
{
	auto managerCall = ReadRelativeTarget(renderFrameCode, codeAddress, k_VideoManagerCall);
	if (!managerCall)
	{
		return managerCall.GetError();
	}
	auto settingsCall = ReadRelativeTarget(renderFrameCode, codeAddress, k_VideoSettingsCall);
	if (!settingsCall)
	{
		return settingsCall.GetError();
	}
	const auto managerCode = codeAt(*managerCall, k_VideoManagerGetterSize);
	if (managerCode.size() != k_VideoManagerGetterSize)
	{
		return ts4::MakeError("the function that gets the video manager is not in the code of the game");
	}
	auto global = ReadRelativeTarget(managerCode, *managerCall, k_VideoManagerGlobal);
	if (!global)
	{
		return global.GetError();
	}
	const auto settingsCode = codeAt(*settingsCall, k_VideoSettingsGetterSize);
	if (settingsCode.size() != k_VideoSettingsGetterSize)
	{
		return ts4::MakeError("the function that gets the video settings is not in the code of the game");
	}
	auto offset = ReadFieldOffset(settingsCode, k_VideoSettingsOffset);
	if (!offset)
	{
		return offset.GetError();
	}
	return VideoSettingsLayout {.managerGlobal = *global, .settingsOffset = *offset};
}

} // namespace ts4_mod::youtube_tv::game_layout
