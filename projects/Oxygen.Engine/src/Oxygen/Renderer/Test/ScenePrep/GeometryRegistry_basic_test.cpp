//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Renderer/ScenePrep/State/GeometryRegistry.h>

namespace {

using oxygen::engine::sceneprep::GeometryHandle;
using oxygen::engine::sceneprep::GeometryRegistry;

// Minimal helper to build a GeometryAsset with one empty mesh LOD.
static auto MakeTestGeometry(const char* name = "TestGeom")
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using namespace oxygen::data;
  pak::GeometryAssetDesc desc {};
  std::snprintf(desc.header.name, sizeof(desc.header.name), "%s", name);
  desc.lod_count = 0; // no meshes required for registration semantics here
  return std::make_shared<GeometryAsset>(
    desc, std::vector<std::shared_ptr<Mesh>> {});
}

NOLINT_TEST(GeometryRegistryBasicTest, GetOrRegisterGeometry_ReusesHandle)
{
  GeometryRegistry registry;
  auto geom = MakeTestGeometry();
  const auto h1 = registry.GetOrRegisterGeometry(geom.get());
  const auto h2 = registry.GetOrRegisterGeometry(geom.get());
  EXPECT_EQ(h1.vertex_buffer, h2.vertex_buffer);
  EXPECT_EQ(h1.index_buffer, h2.index_buffer);
  EXPECT_TRUE(registry.IsValidHandle(h1));
  EXPECT_FALSE(GeometryRegistry::IsSentinelHandle(h1));
  EXPECT_EQ(registry.GetRegisteredGeometryCount(), 1U);
}

NOLINT_TEST(GeometryRegistryBasicTest, NullGeometry_ReturnsSentinelHandle)
{
  GeometryRegistry registry;
  const auto h = registry.GetOrRegisterGeometry(nullptr);
  EXPECT_TRUE(GeometryRegistry::IsSentinelHandle(h));
  EXPECT_FALSE(registry.IsValidHandle(h));
}

NOLINT_TEST(GeometryRegistryBasicTest, LookupHandle_NoRegistration)
{
  GeometryRegistry registry;
  auto geom = MakeTestGeometry("LookupGeom");
  const auto lookup_before = registry.LookupGeometryHandle(geom.get());
  const auto h = registry.GetOrRegisterGeometry(geom.get());
  const auto lookup_after = registry.LookupGeometryHandle(geom.get());
  EXPECT_FALSE(lookup_before.has_value());
  ASSERT_TRUE(lookup_after.has_value());
  EXPECT_EQ(lookup_after->vertex_buffer, h.vertex_buffer);
  EXPECT_EQ(lookup_after->index_buffer, h.index_buffer);
}

} // namespace
