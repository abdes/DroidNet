//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>

#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/Schema/PropertyRange.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace schema = oxygen::data::schema;
using oxygen::data::pak::geometry::LodPolicyType;
using schema::PropertyRange;

NOLINT_TEST(PropertyRangeTest, UnlimitedRangeAcceptsEveryFiniteValue)
{
  constexpr PropertyRange<float> kRange {};

  EXPECT_TRUE(kRange.Contains(std::numeric_limits<float>::lowest()));
  EXPECT_TRUE(kRange.Contains(0.0F));
  EXPECT_FALSE(kRange.Contains(std::numeric_limits<float>::infinity()));
  EXPECT_FALSE(kRange.Contains(std::numeric_limits<float>::quiet_NaN()));
}

NOLINT_TEST(PropertyRangeTest, HonoursExclusiveBoundsAndNonZero)
{
  constexpr PropertyRange<float> kPositive {
    .min = 0.0F,
    .min_exclusive = true,
  };
  constexpr PropertyRange<float> kNonZero { .nonzero = true };
  constexpr PropertyRange<std::int32_t> kCascades { .min = 1, .max = 4 };

  EXPECT_FALSE(kPositive.Contains(0.0F));
  EXPECT_TRUE(kPositive.Contains(0.001F));
  EXPECT_FALSE(kNonZero.Contains(0.0F));
  EXPECT_TRUE(kNonZero.Contains(-1.0F));
  EXPECT_TRUE(kCascades.Contains(4));
  EXPECT_FALSE(kCascades.Contains(5));
}

NOLINT_TEST(PropertyRangeTest, CommitClampsOnlyFiniteValuesOfAClampingRange)
{
  constexpr PropertyRange<float> kClamping {
    .min = 0.0F,
    .max = 1.0F,
    .clamp = true,
  };
  constexpr PropertyRange<float> kRejecting {
    .min = 0.0F,
    .min_exclusive = true,
  };

  EXPECT_EQ(kClamping.Commit(0.5F), 0.5F);
  EXPECT_EQ(kClamping.Commit(2.0F), 1.0F);
  EXPECT_EQ(kClamping.Commit(-1.0F), 0.0F);
  EXPECT_FALSE(
    kClamping.Commit(std::numeric_limits<float>::quiet_NaN()).has_value());
  EXPECT_FALSE(
    kClamping.Commit(std::numeric_limits<float>::infinity()).has_value());
  EXPECT_EQ(kRejecting.Commit(2.0F), 2.0F);
  EXPECT_FALSE(kRejecting.Commit(0.0F).has_value());
}

// The generator refuses a default outside its range; this checks the
// rendering keeps that true in field units.
NOLINT_TEST(SceneSchemaTest, DefaultsLieInsideTheirRangesInFieldUnits)
{
  namespace camera = schema::perspective_camera;
  namespace spot = schema::spot_light;

  EXPECT_FLOAT_EQ(
    camera::kFieldOfViewYRadiansDefault, std::numbers::pi_v<float> / 3.0F);
  EXPECT_TRUE(camera::kFieldOfViewYRadiansRange.Contains(
    camera::kFieldOfViewYRadiansDefault));
  EXPECT_TRUE(spot::kInnerConeAngleRadiansRange.Contains(
    spot::kInnerConeAngleRadiansDefault));
  EXPECT_TRUE(spot::kRangeRange.Contains(spot::kRangeDefault));
  EXPECT_TRUE(schema::directional_light::kCascadeCountRange.Contains(
    schema::directional_light::kCascadeCountDefault));
  EXPECT_FALSE(schema::transform::kScaleXRange.Contains(0.0F));
}

NOLINT_TEST(SceneSchemaTest, NodeFlagDefaultsAreDeclaredMembers)
{
  namespace flags = schema::node_flags;

  EXPECT_TRUE(std::ranges::contains(
    flags::kVisibleValues, std::to_underlying(flags::kVisibleDefault)));
  EXPECT_TRUE(std::ranges::contains(
    flags::kVisibleValues, std::to_underlying(flags::kVisibleRootDefault)));
  EXPECT_NE(flags::kVisibleDefault, flags::kVisibleRootDefault);
}

NOLINT_TEST(SceneSchemaTest, OverridePropertiesAreOrderedByStableId)
{
  const auto& table = schema::kOverrideProperties;

  ASSERT_EQ(table.size(), 89U);
  EXPECT_TRUE(std::ranges::is_sorted(table, {}, &schema::OverrideProperty::id));
  EXPECT_EQ(
    std::ranges::adjacent_find(table, {}, &schema::OverrideProperty::id),
    table.end());
  EXPECT_EQ(table[0].id, schema::OverrideId::kGeometrySurfaceMaterial);
  EXPECT_EQ(table[0].part_kind, schema::PartKind::kSurface);
  EXPECT_EQ(table[0].key, "/material");
  EXPECT_EQ(table[3].id, schema::OverrideId::kGeometryLodPolicy);
  EXPECT_EQ(table[3].part_kind, schema::PartKind::kNone);
  EXPECT_EQ(table[3].type, schema::ValueType::kLodPolicy);
  EXPECT_TRUE(table[3].asset_sourced);
  EXPECT_EQ(table[4].id, schema::OverrideId::kTransformPositionX);
  EXPECT_EQ(table[4].component, schema::ComponentId::kTransform);
  EXPECT_FALSE(table[4].asset_sourced);
}

NOLINT_TEST(SceneSchemaTest, AssetDefaultsShowEverySurfaceAndTheFinestLod)
{
  EXPECT_TRUE(schema::geometry::surface::kVisibilityDefault);
  EXPECT_TRUE(schema::geometry::surface::kCastsShadowsDefault);
  EXPECT_EQ(schema::geometry::kLodPolicyDefault.type, LodPolicyType::kFixed);
  EXPECT_EQ(schema::geometry::kLodPolicyDefault.lod_index, 0U);
  EXPECT_EQ(schema::geometry::kLodPolicyDefault.boundary_count, 0U);
}

} // namespace
