/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cmath>

#include <array>

#include <core/AudioSpace.h>
#include <gtest/gtest.h>

namespace
{

using namespace ts4_mod::youtube_tv;

constexpr float k_Tolerance = 1.0e-5F;
const float k_HalfSqrt2 = std::sqrt(0.5F);

void ExpectNear(Vec3 actual, Vec3 expected)
{
	EXPECT_NEAR(actual.x, expected.x, k_Tolerance);
	EXPECT_NEAR(actual.y, expected.y, k_Tolerance);
	EXPECT_NEAR(actual.z, expected.z, k_Tolerance);
}

TEST(AudioSpace, RotatesWithAQuaternion)
{
	// A quarter turn about y turns +z to +x.
	ExpectNear(Rotate({0.0F, k_HalfSqrt2, 0.0F, k_HalfSqrt2}, {0.0F, 0.0F, 1.0F}), {1.0F, 0.0F, 0.0F});
	// The function makes the quaternion a unit quaternion.
	ExpectNear(Rotate({0.0F, 2.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 1.0F}), {1.0F, 0.0F, 0.0F});
	ExpectNear(Rotate({0.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 2.0F, 3.0F}), {1.0F, 2.0F, 3.0F});
}

TEST(AudioSpace, TheEmitterChangesTheSignOfZForRightHandedAxes)
{
	const ObjectTransform transform {.position = {1.0F, 2.0F, 3.0F}, .orientation = {0.0F, 0.0F, 0.0F, 1.0F}};
	const auto pose = EmitterPose(transform, {});
	ASSERT_TRUE(pose);
	ExpectNear(pose->position, {1.0F, 2.0F, -3.0F});
	ExpectNear(pose->front, {0.0F, 0.0F, -1.0F});
	ExpectNear(pose->top, {0.0F, 1.0F, 0.0F});

	const auto left = EmitterPose(transform, {.rightHanded = false});
	ASSERT_TRUE(left);
	ExpectNear(left->position, {1.0F, 2.0F, 3.0F});
	ExpectNear(left->front, {0.0F, 0.0F, 1.0F});
}

TEST(AudioSpace, TheEmitterTurnsWithTheTv)
{
	const ObjectTransform transform {.position = {0.0F, 0.0F, 0.0F}, .orientation = {0.0F, k_HalfSqrt2, 0.0F, k_HalfSqrt2}};
	const auto pose = EmitterPose(transform, {.rightHanded = false});
	ASSERT_TRUE(pose);
	ExpectNear(pose->front, {1.0F, 0.0F, 0.0F});
	ExpectNear(pose->top, {0.0F, 1.0F, 0.0F});
}

TEST(AudioSpace, TheEmitterRefusesValuesThatAreNotFinite)
{
	const ObjectTransform transform {.position = {NAN, 0.0F, 0.0F}};
	EXPECT_FALSE(EmitterPose(transform, {}));
	EXPECT_FALSE(EmitterPose({}, {.modelFront = {0.0F, 1.0F, 0.0F}, .modelTop = {0.0F, 2.0F, 0.0F}}));
}

TEST(AudioSpace, OrthonormalMakesTheTopAtARightAngle)
{
	const auto pose = Orthonormal({}, {0.0F, 0.0F, 2.0F}, {0.0F, 1.0F, 1.0F});
	ASSERT_TRUE(pose);
	ExpectNear(pose->front, {0.0F, 0.0F, 1.0F});
	ExpectNear(pose->top, {0.0F, 1.0F, 0.0F});
	EXPECT_FALSE(Orthonormal({}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}));
}

// A camera at (4, 5, 6), turned a quarter turn about y: its right axis is -z, and its back axis is +x.
constexpr std::array<float, 16> k_WorldRows {0.0F, 0.0F, -1.0F, 0.0F, //
                                             0.0F, 1.0F, 0.0F,  0.0F, //
                                             1.0F, 0.0F, 0.0F,  0.0F, //
                                             4.0F, 5.0F, 6.0F,  1.0F};

TEST(AudioSpace, TheListenerReadsAWorldMatrixInRows)
{
	const auto pose = ListenerPose(k_WorldRows, {.rightHanded = false});
	ASSERT_TRUE(pose);
	ExpectNear(pose->position, {4.0F, 5.0F, 6.0F});
	// The camera looks along its negative z axis.
	ExpectNear(pose->front, {-1.0F, 0.0F, 0.0F});
	ExpectNear(pose->top, {0.0F, 1.0F, 0.0F});

	const auto forward = ListenerPose(k_WorldRows, {.rightHanded = false, .cameraLooksAlongNegativeZ = false});
	ASSERT_TRUE(forward);
	ExpectNear(forward->front, {1.0F, 0.0F, 0.0F});
}

TEST(AudioSpace, TheListenerReadsTheOtherLayouts)
{
	std::array<float, 16> columns {};
	for (int row = 0; row < 4; ++row)
	{
		for (int column = 0; column < 4; ++column)
		{
			columns[column * 4 + row] = k_WorldRows[row * 4 + column];
		}
	}
	const auto fromColumns = ListenerPose(columns, {.rightHanded = false, .cameraLayout = CameraMatrixLayout::WorldColumns});
	ASSERT_TRUE(fromColumns);
	ExpectNear(fromColumns->position, {4.0F, 5.0F, 6.0F});
	ExpectNear(fromColumns->front, {-1.0F, 0.0F, 0.0F});

	// The view matrix: the transpose of the rotation, and the position -p R^T.
	const std::array<float, 16> view {0.0F,  0.0F,  1.0F,  0.0F, //
	                                  0.0F,  1.0F,  0.0F,  0.0F, //
	                                  -1.0F, 0.0F,  0.0F,  0.0F, //
	                                  6.0F,  -5.0F, -4.0F, 1.0F};
	const auto fromView = ListenerPose(view, {.rightHanded = false, .cameraLayout = CameraMatrixLayout::ViewRows});
	ASSERT_TRUE(fromView);
	ExpectNear(fromView->position, {4.0F, 5.0F, 6.0F});
	ExpectNear(fromView->front, {-1.0F, 0.0F, 0.0F});
	ExpectNear(fromView->top, {0.0F, 1.0F, 0.0F});
}

TEST(AudioSpace, TheListenerRefusesAMatrixWithAScale)
{
	auto scaled = k_WorldRows;
	scaled[0] = 0.0F;
	scaled[2] = -3.0F;
	EXPECT_FALSE(ListenerPose(scaled, {}));
	auto bad = k_WorldRows;
	bad[5] = INFINITY;
	EXPECT_FALSE(ListenerPose(bad, {}));
}

TEST(AudioSpace, ReadsTheNamesOfTheLayouts)
{
	EXPECT_EQ(ParseCameraMatrixLayout("viewColumns"), CameraMatrixLayout::ViewColumns);
	EXPECT_FALSE(ParseCameraMatrixLayout("rows"));
	EXPECT_STREQ(CameraMatrixLayoutName(CameraMatrixLayout::WorldRows), "worldRows");
}

TEST(AudioSpace, TheVoiceIsSilentWhenTheVideoDoesNotPlay)
{
	EXPECT_FLOAT_EQ(VoiceVolume(0.8, false, SessionState::Playing), 0.8F);
	EXPECT_FLOAT_EQ(VoiceVolume(0.8, true, SessionState::Playing), 0.0F);
	EXPECT_FLOAT_EQ(VoiceVolume(0.8, false, SessionState::Paused), 0.0F);
	EXPECT_FLOAT_EQ(VoiceVolume(0.8, false, SessionState::Buffering), 0.0F);
	EXPECT_FLOAT_EQ(VoiceVolume(3.0, false, SessionState::Playing), 1.0F);
	EXPECT_FLOAT_EQ(VoiceVolume(NAN, false, SessionState::Playing), 0.0F);
}

} // namespace
