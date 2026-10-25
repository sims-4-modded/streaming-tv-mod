/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "AudioSpace.h"

#include <cmath>

#include <algorithm>

namespace ts4_mod::youtube_tv
{

namespace
{

/// The smallest length of a vector that the mod makes into a unit vector.
constexpr float k_MinLength = 1.0e-4F;

[[nodiscard]] bool Finite(Vec3 a)
{
	return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

[[nodiscard]] std::optional<Vec3> Unit(Vec3 a)
{
	const auto length = Length(a);
	if (!Finite(a) || !(length > k_MinLength))
	{
		return std::nullopt;
	}
	return a * (1.0F / length);
}

/// One row or one column of the 3x3 part and the position of a matrix.
[[nodiscard]] Vec3 Axis(std::span<const float, 16> m, int index, bool rows)
{
	if (rows)
	{
		return {m[index * 4 + 0], m[index * 4 + 1], m[index * 4 + 2]};
	}
	return {m[0 * 4 + index], m[1 * 4 + index], m[2 * 4 + index]};
}

} // namespace

Vec3 operator+(Vec3 a, Vec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(Vec3 a, float scale)
{
	return {a.x * scale, a.y * scale, a.z * scale};
}

float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float Length(Vec3 a)
{
	return std::sqrt(Dot(a, a));
}

Vec3 Rotate(const std::array<float, 4>& quaternion, Vec3 vector)
{
	const auto [qx, qy, qz, qw] = quaternion;
	const float length = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
	if (!std::isfinite(length) || length < k_MinLength)
	{
		return vector;
	}
	const Vec3 axis {qx / length, qy / length, qz / length};
	const float w = qw / length;
	// v' = v + 2w (u x v) + 2 u x (u x v), with u the vector part of the unit quaternion.
	const auto uv = Cross(axis, vector);
	const auto uuv = Cross(axis, uv);
	return vector + uv * (2.0F * w) + uuv * 2.0F;
}

std::optional<CameraMatrixLayout> ParseCameraMatrixLayout(std::string_view text)
{
	for (const auto layout : {CameraMatrixLayout::WorldRows, CameraMatrixLayout::WorldColumns, CameraMatrixLayout::ViewRows,
	                          CameraMatrixLayout::ViewColumns})
	{
		if (text == CameraMatrixLayoutName(layout))
		{
			return layout;
		}
	}
	return std::nullopt;
}

const char* CameraMatrixLayoutName(CameraMatrixLayout layout)
{
	switch (layout)
	{
	case CameraMatrixLayout::WorldRows:
		return "worldRows";
	case CameraMatrixLayout::WorldColumns:
		return "worldColumns";
	case CameraMatrixLayout::ViewRows:
		return "viewRows";
	case CameraMatrixLayout::ViewColumns:
		return "viewColumns";
	}
	return "unknown";
}

Vec3 ToAudioAxes(Vec3 vector, const AxisConvention& axes)
{
	return axes.rightHanded ? Vec3 {vector.x, vector.y, -vector.z} : vector;
}

std::optional<AudioPose> Orthonormal(Vec3 position, Vec3 front, Vec3 top)
{
	if (!Finite(position))
	{
		return std::nullopt;
	}
	const auto unitFront = Unit(front);
	if (!unitFront)
	{
		return std::nullopt;
	}
	// Remove the part of the top vector along the front vector.
	const auto unitTop = Unit(top - *unitFront * Dot(top, *unitFront));
	if (!unitTop)
	{
		return std::nullopt;
	}
	return AudioPose {.position = position, .front = *unitFront, .top = *unitTop};
}

std::optional<AudioPose> EmitterPose(const ObjectTransform& transform, const AxisConvention& axes)
{
	const Vec3 position {transform.position[0], transform.position[1], transform.position[2]};
	const auto front = Rotate(transform.orientation, axes.modelFront);
	const auto top = Rotate(transform.orientation, axes.modelTop);
	return Orthonormal(ToAudioAxes(position, axes), ToAudioAxes(front, axes), ToAudioAxes(top, axes));
}

std::optional<AudioPose> ListenerPose(std::span<const float, 16> matrix, const AxisConvention& axes)
{
	if (!std::ranges::all_of(matrix, [](float value) { return std::isfinite(value); }))
	{
		return std::nullopt;
	}
	const bool rows = axes.cameraLayout == CameraMatrixLayout::WorldRows || axes.cameraLayout == CameraMatrixLayout::ViewRows;
	const bool view = axes.cameraLayout == CameraMatrixLayout::ViewRows || axes.cameraLayout == CameraMatrixLayout::ViewColumns;
	Vec3 right = Axis(matrix, 0, rows);
	Vec3 up = Axis(matrix, 1, rows);
	Vec3 back = Axis(matrix, 2, rows);
	Vec3 position = Axis(matrix, 3, rows);
	if (view)
	{
		// The inverse of a rotation and a position: the transpose of the rotation, and the position turned back.
		const Vec3 r0 = right;
		const Vec3 r1 = up;
		const Vec3 r2 = back;
		right = {r0.x, r1.x, r2.x};
		up = {r0.y, r1.y, r2.y};
		back = {r0.z, r1.z, r2.z};
		position = (right * position.x + up * position.y + back * position.z) * -1.0F;
	}
	const auto front = axes.cameraLooksAlongNegativeZ ? back * -1.0F : back;
	// A rotation keeps the lengths of its axes. A matrix with a scale is not a camera that the mod knows.
	if (std::abs(Length(right) - 1.0F) > 0.05F || std::abs(Length(up) - 1.0F) > 0.05F || std::abs(Length(back) - 1.0F) > 0.05F)
	{
		return std::nullopt;
	}
	return Orthonormal(ToAudioAxes(position, axes), ToAudioAxes(front, axes), ToAudioAxes(up, axes));
}

float VoiceVolume(double settingsVolume, bool muted, SessionState state)
{
	if (muted || state != SessionState::Playing || !std::isfinite(settingsVolume))
	{
		return 0.0F;
	}
	return static_cast<float>(std::clamp(settingsVolume, 0.0, 1.0));
}

} // namespace ts4_mod::youtube_tv
