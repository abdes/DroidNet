//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <array>

#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowRequest.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.h>

namespace {
using namespace oxygen;
using namespace oxygen::vortex;
using namespace oxygen::vortex::shadows::internal;

struct CanonicalShadowRequestTest : ::testing::Test {
  ShadowCasterDependencies pool;
  std::array<float, 16> world { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    1 };
  std::array<ShadowCasterSource, 1> sources {};
  std::array<MaterialShadingConstants, 1> materials {};
  std::array<uint64_t, 1> revisions { 1 };
  PreparedSceneFrame scene;
  std::vector<ShadowCasterDependency> dependencies;
  FrameLocalLightSelection light;
  auto SetUp() -> void override
  {
    sources[0].node = scene::NodeHandle { 2, 1 };
    sources[0].bounds = glm::vec4(0, 0, -2, 0.5F);
    sources[0].geometry_content_revision = 1;
    sources[0].draw.vertex_count = 3;
    scene.world_matrices = world;
    scene.shadow_caster_sources = sources;
    scene.shadow_materials = materials;
    scene.shadow_texture_revisions = revisions;
    light.source_node = scene::NodeHandle { 3, 1 };
    light.range = 10;
    light.direction = glm::vec3(0, 0, -1);
    light.flags = kLocalLightFlagCastsShadows;
    pool.Build(scene, dependencies);
  }
  auto Request() -> LocalShadowRequest
  {
    LocalShadowRequest request;
    PreparedViewShadowInput view { .scene_generation = 7,
      .shadow_caster_dependencies = dependencies,
      .shadow_dependencies_available = true };
    PrepareLocalShadowRequest(
      view, light, LightSelectionIndex { 0 }, 512, 1, request);
    return request;
  }
};
NOLINT_TEST_F(CanonicalShadowRequestTest,
  ForcedHashCollisionStillRequiresExactCasterEquality)
{
  auto first = ShadowCasterRecord {};
  first.geometry_revision = 1;
  auto second = first;
  second.geometry_revision = 2;
  const auto a = pool.Intern(first, 123);
  const auto b = pool.Intern(second, 123);
  EXPECT_NE(a, b);
  EXPECT_EQ(pool.Intern(first, 123), a);
  EXPECT_EQ(pool.Intern(second, 123), b);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, RebuildingSnapshotReusesUnchangedRecords)
{
  const auto original = dependencies[0].record;
  pool.Build(scene, dependencies);
  EXPECT_EQ(dependencies[0].record, original);
  world[12] = 2;
  pool.Build(scene, dependencies);
  EXPECT_NE(dependencies[0].record, original);
  EXPECT_NE(*dependencies[0].record, *original);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, UnknownSampledTextureContinuityDisablesReuse)
{
  sources[0].draw.flags.Set(PassMaskBit::kMasked);
  materials[0].base_color_texture_index = ShaderVisibleIndex { 5 };
  revisions[0] = 0;
  pool.Build(scene, dependencies);
  EXPECT_FALSE(dependencies[0].reusable);
  EXPECT_FALSE(Request().content.reusable);
  materials[0].flags = data::pak::render::kMaterialFlag_NoTextureSampling;
  pool.Build(scene, dependencies);
  EXPECT_TRUE(dependencies[0].reusable);
  EXPECT_TRUE(Request().content.reusable);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, RelevantCasterSetIsIndependentOfSelectionOrder)
{
  auto second = *dependencies[0].record;
  second.submesh = 1;
  auto dependency = dependencies[0];
  dependency.record
    = pool.Intern(second, dependencies[0].fingerprint); // forced collision
  dependencies.push_back(dependency);
  const auto original = Request();
  std::ranges::reverse(dependencies);
  const auto reordered = Request();
  EXPECT_EQ(original.content.hash, reordered.content.hash);
  EXPECT_EQ(original.content, reordered.content);
  dependencies.pop_back();
  EXPECT_NE(original.content, Request().content);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, MembershipIncludesNewAndUnknownBoundCasters)
{
  dependencies[0].bounds = glm::vec4(100, 0, 0, 1);
  EXPECT_TRUE(Request().content.casters.empty());
  dependencies[0].bounds = glm::vec4(0, 0, -2, 1);
  EXPECT_EQ(Request().content.casters.size(), 1U);
  dependencies[0].bounds = glm::vec4(0); // unknown: conservatively keep
  EXPECT_EQ(Request().content.casters.size(), 1U);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, ConsumerOnlyPointBiasDoesNotInvalidateDepth)
{
  const auto original = Request();
  light.shadow_bias = 4;
  light.shadow_normal_bias = 2;
  const auto changed = Request();
  EXPECT_EQ(original.content, changed.content);
  EXPECT_EQ(original.content.hash, changed.content.hash);
  EXPECT_NE(std::get<CubeLocalShadowRecord>(original.projection).depth_bias,
    std::get<CubeLocalShadowRecord>(changed.projection).depth_bias);
  light.kind = LocalLightKind::kSpot;
  light.shadow_bias = 0.01F;
  const auto spot = Request();
  light.shadow_bias = 0.02F;
  EXPECT_NE(spot.content, Request().content);
}
NOLINT_TEST_F(
  CanonicalShadowRequestTest, GeometryLodAndRasterChangesInvalidateContent)
{
  const auto original = Request();
  sources[0].lod_index = 1;
  pool.Build(scene, dependencies);
  EXPECT_NE(original.content, Request().content);
  sources[0].lod_index = 0;
  sources[0].draw.flags.Set(PassMaskBit::kDoubleSided);
  pool.Build(scene, dependencies);
  EXPECT_NE(original.content, Request().content);
  sources[0].draw.flags = {};
  sources[0].geometry_content_revision = 2;
  pool.Build(scene, dependencies);
  EXPECT_NE(original.content, Request().content);
}
} // namespace
