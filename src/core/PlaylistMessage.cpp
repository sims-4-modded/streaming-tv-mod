/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "PlaylistMessage.h"

#include <optional>

namespace ts4_mod::youtube_tv::playlist
{

namespace
{

/// The wire types of protocol buffers.
enum class WireType : std::uint8_t
{
	Varint = 0,
	Fixed64 = 1,
	Bytes = 2,
	Fixed32 = 5,
};

/// Reads the fields of one message from the start to the end of its bytes.
class FieldReader
{
public:
	explicit FieldReader(std::span<const std::byte> bytes)
	    : _bytes(bytes)
	{
	}

	[[nodiscard]] bool AtEnd() const { return _at == _bytes.size(); }

	[[nodiscard]] std::optional<std::uint64_t> Varint()
	{
		std::uint64_t value = 0;
		for (unsigned shift = 0; shift < 64; shift += 7)
		{
			if (_at >= _bytes.size())
			{
				return std::nullopt;
			}
			const auto byte = std::to_integer<std::uint8_t>(_bytes[_at++]);
			value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
			if ((byte & 0x80) == 0)
			{
				return value;
			}
		}
		return std::nullopt;
	}

	/// Skip `size` bytes, or return false if the message has fewer bytes.
	[[nodiscard]] bool Skip(std::uint64_t size)
	{
		if (size > _bytes.size() - _at)
		{
			return false;
		}
		_at += static_cast<std::size_t>(size);
		return true;
	}

	/// The next `size` bytes, or nothing if the message has fewer bytes.
	[[nodiscard]] std::optional<std::span<const std::byte>> Take(std::uint64_t size)
	{
		const auto start = _at;
		if (!Skip(size))
		{
			return std::nullopt;
		}
		return _bytes.subspan(start, static_cast<std::size_t>(size));
	}

	/// Skip the value of a field that the mod does not read.
	[[nodiscard]] bool SkipValue(WireType type)
	{
		switch (type)
		{
		case WireType::Varint:
			return Varint().has_value();
		case WireType::Fixed64:
			return Skip(8);
		case WireType::Fixed32:
			return Skip(4);
		case WireType::Bytes:
			if (const auto size = Varint())
			{
				return Skip(*size);
			}
			return false;
		}
		return false;
	}

private:
	std::span<const std::byte> _bytes;
	std::size_t _at = 0;
};

struct FieldTag
{
	std::uint32_t number = 0;
	WireType type = WireType::Varint;
};

[[nodiscard]] std::optional<FieldTag> ReadTag(FieldReader& reader)
{
	const auto tag = reader.Varint();
	if (!tag || (*tag >> 3) == 0 || (*tag >> 3) > 0x1FFFFFFF)
	{
		return std::nullopt;
	}
	const auto type = static_cast<std::uint8_t>(*tag & 7);
	if (type != 0 && type != 1 && type != 2 && type != 5)
	{
		return std::nullopt;
	}
	return FieldTag {.number = static_cast<std::uint32_t>(*tag >> 3), .type = static_cast<WireType>(type)};
}

[[nodiscard]] std::optional<ScreenKey> ReadKey(std::span<const std::byte> bytes)
{
	FieldReader reader(bytes);
	ScreenKey key;
	while (!reader.AtEnd())
	{
		const auto tag = ReadTag(reader);
		if (!tag)
		{
			return std::nullopt;
		}
		const bool known = tag->number == k_KeyTypeField || tag->number == k_KeyGroupField || tag->number == k_KeyInstanceField;
		if (!known || tag->type != WireType::Varint)
		{
			if (!reader.SkipValue(tag->type))
			{
				return std::nullopt;
			}
			continue;
		}
		const auto value = reader.Varint();
		if (!value)
		{
			return std::nullopt;
		}
		if (tag->number == k_KeyTypeField)
		{
			key.type = static_cast<std::uint32_t>(*value);
		}
		else if (tag->number == k_KeyGroupField)
		{
			key.group = static_cast<std::uint32_t>(*value);
		}
		else
		{
			key.instance = *value;
		}
	}
	return key;
}

void PutVarint(std::vector<std::byte>& out, std::uint64_t value)
{
	while (value >= 0x80)
	{
		out.push_back(static_cast<std::byte>((value & 0x7F) | 0x80));
		value >>= 7;
	}
	out.push_back(static_cast<std::byte>(value));
}

void PutTag(std::vector<std::byte>& out, std::uint32_t number, WireType type)
{
	PutVarint(out, (static_cast<std::uint64_t>(number) << 3) | static_cast<std::uint8_t>(type));
}

} // namespace

std::vector<std::byte> MakeMessage(std::uint32_t version, std::span<const ScreenKey> keys)
{
	constexpr std::uint32_t k_VersionField = 1;
	std::vector<std::byte> out;
	PutTag(out, k_VersionField, WireType::Varint);
	PutVarint(out, version);
	for (const auto& key : keys)
	{
		std::vector<std::byte> inner;
		PutTag(inner, k_KeyTypeField, WireType::Varint);
		PutVarint(inner, key.type);
		PutTag(inner, k_KeyGroupField, WireType::Varint);
		PutVarint(inner, key.group);
		PutTag(inner, k_KeyInstanceField, WireType::Varint);
		PutVarint(inner, key.instance);
		PutTag(out, k_ClipKeysField, WireType::Bytes);
		PutVarint(out, inner.size());
		out.insert(out.end(), inner.begin(), inner.end());
	}
	return out;
}

ts4::Result<std::vector<ScreenKey>> ReadClipKeys(std::span<const std::byte> message)
{
	if (message.size() > k_MaxMessageBytes)
	{
		return ts4::MakeError("the playlist message has {} bytes, more than the limit", message.size());
	}
	FieldReader reader(message);
	std::vector<ScreenKey> keys;
	while (!reader.AtEnd())
	{
		const auto tag = ReadTag(reader);
		if (!tag)
		{
			return ts4::MakeError("the playlist message has a field that is not correct");
		}
		if (tag->number != k_ClipKeysField || tag->type != WireType::Bytes)
		{
			if (!reader.SkipValue(tag->type))
			{
				return ts4::MakeError("the playlist message ends in a field");
			}
			continue;
		}
		const auto size = reader.Varint();
		const auto bytes = size ? reader.Take(*size) : std::nullopt;
		if (!bytes)
		{
			return ts4::MakeError("the playlist message ends in a clip key");
		}
		const auto key = ReadKey(*bytes);
		if (!key)
		{
			return ts4::MakeError("a clip key of the playlist message is not correct");
		}
		if (keys.size() >= k_MaxClipKeys)
		{
			return ts4::MakeError("the playlist message has more than {} clip keys", k_MaxClipKeys);
		}
		keys.push_back(*key);
	}
	return keys;
}

} // namespace ts4_mod::youtube_tv::playlist
