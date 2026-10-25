/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

#include <core/Sessions.h>

/// The positions and the axes of the 3D audio. The mod changes the transform of a TV and the camera matrix of the
/// game into the emitter and the listener of X3DAudio. X3DAudio uses left-handed axes: x to the right, y up, z to the
/// front. The axes of the game are settings, because only a game test can confirm them.
namespace ts4_mod::youtube_tv
{

struct Vec3
{
	float x = 0.0F;
	float y = 0.0F;
	float z = 0.0F;

	[[nodiscard]] bool operator==(const Vec3&) const = default;
};

[[nodiscard]] Vec3 operator+(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 operator-(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 operator*(Vec3 a, float scale);
[[nodiscard]] float Dot(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 Cross(Vec3 a, Vec3 b);
[[nodiscard]] float Length(Vec3 a);

/// Rotate a vector with a quaternion (x, y, z, w). The function makes the quaternion a unit quaternion first. A
/// quaternion of length 0 gives the vector with no change.
[[nodiscard]] Vec3 Rotate(const std::array<float, 4>& quaternion, Vec3 vector);

/// The layout of the camera matrix of the game: 16 floats.
enum class CameraMatrixLayout
{
	WorldRows,    ///< The rows are the right, up and back (or front) axes, and the position, in world space.
	WorldColumns, ///< The same in the columns.
	ViewRows,     ///< The inverse of `WorldRows`: the matrix changes world space into camera space.
	ViewColumns,  ///< The inverse of `WorldColumns`.
};

[[nodiscard]] std::optional<CameraMatrixLayout> ParseCameraMatrixLayout(std::string_view text);
[[nodiscard]] const char* CameraMatrixLayoutName(CameraMatrixLayout layout);

/// The axes of the game.
struct AxisConvention
{
	/// True if the world of the game uses right-handed axes. Then the mod changes the sign of z for X3DAudio.
	bool rightHanded = true;
	/// The front and the top of a TV model, in the axes of the model.
	Vec3 modelFront {0.0F, 0.0F, 1.0F};
	Vec3 modelTop {0.0F, 1.0F, 0.0F};
	CameraMatrixLayout cameraLayout = CameraMatrixLayout::WorldRows;
	/// True if the camera looks along its negative z axis (the usual rule of right-handed cameras).
	bool cameraLooksAlongNegativeZ = true;

	[[nodiscard]] bool operator==(const AxisConvention&) const = default;
};

/// A position with a front and a top vector in the axes of X3DAudio. `front` and `top` are unit vectors at a right
/// angle.
struct AudioPose
{
	Vec3 position;
	Vec3 front {0.0F, 0.0F, 1.0F};
	Vec3 top {0.0F, 1.0F, 0.0F};
};

/// Change a vector of the game into the axes of X3DAudio.
[[nodiscard]] Vec3 ToAudioAxes(Vec3 vector, const AxisConvention& axes);

/// Make a front and a top vector into unit vectors at a right angle. The result is empty if a vector has the length
/// 0, if a value is not finite, or if the two vectors are parallel.
[[nodiscard]] std::optional<AudioPose> Orthonormal(Vec3 position, Vec3 front, Vec3 top);

/// The emitter of a TV: its position, and the front and the top of its model turned by its orientation.
[[nodiscard]] std::optional<AudioPose> EmitterPose(const ObjectTransform& transform, const AxisConvention& axes);

/// The listener at the camera of the game. The result is empty for a matrix that is not a rotation and a position.
[[nodiscard]] std::optional<AudioPose> ListenerPose(std::span<const float, 16> matrix, const AxisConvention& axes);

/// The volume of the voice of one video: the volume of the settings when the video plays and is not muted, else 0.
[[nodiscard]] float VoiceVolume(double settingsVolume, bool muted, SessionState state);

} // namespace ts4_mod::youtube_tv
