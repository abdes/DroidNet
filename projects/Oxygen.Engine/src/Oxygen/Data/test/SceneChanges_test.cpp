//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/SceneRecordFields.h>
#include <Oxygen/Data/Schema/Generated.SceneChanges.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace schema = oxygen::data::schema;
namespace world = oxygen::data::pak::world;
using schema::OverrideId;
using schema::SchemaValue;

//! Candidate values of each change value member; every changeable property
//! accepts one of its member's candidates.
auto Candidates(const schema::ValueMember member) -> std::vector<SchemaValue>
{
  switch (member) {
  case schema::ValueMember::kBool:
    return { true };
  case schema::ValueMember::kInt32:
    return { std::int32_t { 1 }, std::int32_t { 0 }, std::int32_t { 2 } };
  case schema::ValueMember::kFloat:
    return { 0.5F, 1.0F, 10.0F, 200.0F };
  case schema::ValueMember::kVec3:
  case schema::ValueMember::kQuat:
    return { std::array { 0.5F, 0.5F, 0.5F } };
  case schema::ValueMember::kAsset:
  case schema::ValueMember::kStringOffset:
  case schema::ValueMember::kRecordIndex:
    break;
  }
  return {};
}

//! Applies `value` to a default record of the wired `component`.
auto ApplyToRecordOf(const schema::ComponentId component, const OverrideId id,
  const world::OverrideValue& value) -> bool
{
  switch (component) {
  case schema::ComponentId::kTransform:
  case schema::ComponentId::kNode: {
    auto record = world::NodeRecord {};
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kPerspectiveCamera: {
    auto record = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kOrthographicCamera: {
    auto record = world::MakeDefaultRecord<world::OrthographicCameraRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kDirectionalLight: {
    auto record = world::MakeDefaultRecord<world::DirectionalLightRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kPointLight: {
    auto record = world::MakeDefaultRecord<world::PointLightRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kSpotLight: {
    auto record = world::MakeDefaultRecord<world::SpotLightRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kLocalFogVolume: {
    auto record = world::MakeDefaultRecord<world::LocalFogVolumeRecord>();
    return schema::ApplyChange(record, id, value);
  }
  case schema::ComponentId::kInvalid:
    break;
  }
  return false;
}

//! Encodes a valid value of `property` and applies it to its record.
auto ExpectApplied(const schema::OverrideProperty& property) -> void
{
  auto encoded = std::optional<world::OverrideValue> {};
  for (const auto& candidate : Candidates(property.member)) {
    encoded = schema::EncodeChange(property.id, candidate);
    if (encoded.has_value()) {
      break;
    }
  }
  if (!encoded.has_value()) {
    FAIL() << "no candidate value encodes";
  }
  EXPECT_TRUE(ApplyToRecordOf(property.component, property.id, *encoded));
}

//! Every wired property encodes a valid value and its record applies it: a
//! field the schema adds without a writer fails here.
NOLINT_TEST(SceneChangesTest, EveryWiredPropertyIsAppliedByItsRecord)
{
  for (const auto& property : schema::kOverrideProperties) {
    if (property.component != schema::ComponentId::kInvalid) {
      TRACE_GCHECK_F(ExpectApplied(property), property.key);
    }
  }
}

//! A rotation changes whole: its change holds the quaternion of the three
//! Y-X-Z angles, and the node record receives it.
NOLINT_TEST(SceneChangesTest, RotationChangeHoldsTheQuaternionOfItsAngles)
{
  const auto encoded = schema::EncodeChange(
    OverrideId::kTransformRotation, std::array { 0.0F, 90.0F, 0.0F });
  if (!encoded.has_value()) {
    FAIL() << "the rotation does not encode";
  }
  auto record = world::NodeRecord {};

  ASSERT_TRUE(
    schema::ApplyChange(record, OverrideId::kTransformRotation, *encoded));

  const auto half = std::numbers::sqrt2_v<float> / 2.0F;
  EXPECT_NEAR(record.rotation[0], 0.0F, 1e-6F);
  EXPECT_NEAR(record.rotation[1], half, 1e-6F);
  EXPECT_NEAR(record.rotation[2], 0.0F, 1e-6F);
  EXPECT_NEAR(record.rotation[3], half, 1e-6F);
}

//! Yaw applies before pitch and roll, as the editor composes them.
NOLINT_TEST(SceneChangesTest, EulerAnglesComposeYawPitchRoll)
{
  const auto pitch = world::EulerYxzDegreesToQuaternion({ 90.0F, 0.0F, 0.0F });
  const auto yaw_pitch
    = world::EulerYxzDegreesToQuaternion({ 90.0F, 90.0F, 0.0F });

  const auto half = std::numbers::sqrt2_v<float> / 2.0F;
  EXPECT_NEAR(pitch[0], half, 1e-6F);
  EXPECT_NEAR(pitch[3], half, 1e-6F);
  // Y(90) * X(90) = (0.5, 0.5, -0.5, 0.5).
  EXPECT_NEAR(yaw_pitch[0], 0.5F, 1e-6F);
  EXPECT_NEAR(yaw_pitch[1], 0.5F, 1e-6F);
  EXPECT_NEAR(yaw_pitch[2], -0.5F, 1e-6F);
  EXPECT_NEAR(yaw_pitch[3], 0.5F, 1e-6F);
}

//! An angle in degrees reaches its field in radians.
NOLINT_TEST(SceneChangesTest, AngleChangeIsConvertedToRadians)
{
  const auto encoded = schema::EncodeChange(
    OverrideId::kPerspectiveCameraFieldOfViewDegrees, 90.0F);
  if (!encoded.has_value()) {
    FAIL() << "the field of view does not encode";
  }
  auto camera = world::MakeDefaultRecord<world::PerspectiveCameraRecord>();

  ASSERT_TRUE(schema::ApplyChange(
    camera, OverrideId::kPerspectiveCameraFieldOfViewDegrees, *encoded));

  EXPECT_FLOAT_EQ(camera.fov_y, std::numbers::pi_v<float> / 2.0F);
}

//! A flag source change follows the tri-state rule: Inherit sets the
//! inherited bit, Shown the local bit, Hidden neither.
NOLINT_TEST(SceneChangesTest, FlagSourceChangeFollowsTheTriStateRule)
{
  const auto apply
    = [](const schema::VisibilitySource source) -> world::NodeRecord {
    auto record = world::NodeRecord {};
    record.node_flags = world::kSceneNodeFlag_Visible;
    const auto encoded = schema::EncodeChange(
      OverrideId::kNodeVisibility, static_cast<std::int32_t>(source));
    EXPECT_TRUE(encoded.has_value()
      && schema::ApplyChange(record, OverrideId::kNodeVisibility, *encoded));
    return record;
  };

  const auto inherit = apply(schema::VisibilitySource::kInherit);
  const auto shown = apply(schema::VisibilitySource::kShown);
  const auto hidden = apply(schema::VisibilitySource::kHidden);

  EXPECT_EQ(inherit.node_flags, 0U);
  EXPECT_EQ(inherit.inherited_flags, world::kSceneNodeFlag_Visible);
  EXPECT_EQ(shown.node_flags, world::kSceneNodeFlag_Visible);
  EXPECT_EQ(shown.inherited_flags, 0U);
  EXPECT_EQ(hidden.node_flags, 0U);
  EXPECT_EQ(hidden.inherited_flags, 0U);
  EXPECT_TRUE(world::HasCanonicalNodeFlags(inherit));
}

//! The orthographic size rewrites the volume and keeps its aspect ratio.
NOLINT_TEST(SceneChangesTest, OrthographicSizeKeepsTheAspectRatio)
{
  const auto encoded = schema::EncodeChange(
    OverrideId::kOrthographicCameraOrthographicSize, 5.0F);
  if (!encoded.has_value()) {
    FAIL() << "the size does not encode";
  }
  auto camera = world::MakeDefaultRecord<world::OrthographicCameraRecord>();

  ASSERT_TRUE(schema::ApplyChange(
    camera, OverrideId::kOrthographicCameraOrthographicSize, *encoded));

  EXPECT_FLOAT_EQ(camera.top, 5.0F);
  EXPECT_FLOAT_EQ(camera.bottom, -5.0F);
  EXPECT_FLOAT_EQ(
    camera.right, 5.0F * schema::orthographic_camera::kAspectRatioDefault);
  EXPECT_TRUE(world::HasValidOrthographicCameraValues(camera));
}

//! A value outside its property's range, of another type, or of a property
//! whose values a change does not hold, encodes to nothing.
NOLINT_TEST(SceneChangesTest, InvalidValuesDoNotEncode)
{
  EXPECT_FALSE(
    schema::EncodeChange(OverrideId::kPerspectiveCameraFieldOfViewDegrees, 0.0F)
      .has_value());
  EXPECT_FALSE(
    schema::EncodeChange(OverrideId::kTransformScaleX, 0.0F).has_value());
  EXPECT_FALSE(
    schema::EncodeChange(OverrideId::kNodeVisibility, std::int32_t { 7 })
      .has_value());
  EXPECT_FALSE(
    schema::EncodeChange(OverrideId::kNodeIsStatic, 1.0F).has_value());
  EXPECT_FALSE(
    schema::EncodeChange(OverrideId::kGeometryLodPolicy, true).has_value());
  EXPECT_FALSE(schema::EncodeChange(OverrideId::kInvalid, true).has_value());
}

//! A record refuses the change of a property it does not hold.
NOLINT_TEST(SceneChangesTest, RecordRefusesAnotherComponentsProperty)
{
  auto value = world::OverrideValue {};
  value.as_float = 0.5F;
  auto point = world::MakeDefaultRecord<world::PointLightRecord>();
  auto node = world::NodeRecord {};

  EXPECT_FALSE(schema::ApplyChange(
    point, OverrideId::kSpotLightInnerConeAngleRadians, value));
  EXPECT_FALSE(
    schema::ApplyChange(node, OverrideId::kPerspectiveCameraIso, value));
}

} // namespace
