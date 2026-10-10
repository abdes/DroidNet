//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::GeometrySource;

  //! A sphere resolution far beyond any generator limit.
  constexpr auto kHugeSegmentCount = 1'000'000;

  auto MakeDescriptor() -> nlohmann::json
  {
    return nlohmann::json::parse(R"({
      "name":"Geometry.v2","content_hashing":false,
      "bounds":{"min":[-1,-1,-1],"max":[1,1,1]},
      "buffers":[{"uri":"raw/vertices.bin",
        "virtual_path":"/Content/Buffers/vertices.obuf"}],
      "lods":[{
        "name":"LOD0","mesh_type":"standard",
        "bounds":{"min":[-1,-1,-1],"max":[1,1,1]},
        "buffers":{"vb_ref":"/Content/Buffers/vertices.obuf",
          "ib_ref":"/Shared/Buffers/indices.obuf"},
        "submeshes":[{
          "slot_id":"018f8f8f-1111-7111-8111-111111111111",
          "material_ref":"/Content/Materials/stone.omat",
          "views":[{"view_ref":"surface"}]
        }]
      }]
    })");
  }

  NOLINT_TEST(
    GeometrySourceTest, RetainsSourcesAndSymbolicReferencesWithoutCookedRoots)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      MakeDescriptor().dump(), "authored/geometry.json", diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(source->name, "Geometry.v2");
    if (!source->content_hashing.has_value()) {
      FAIL() << "Expected source->content_hashing to contain a value";
    }
    EXPECT_FALSE(*source->content_hashing);
    ASSERT_EQ(source->buffers.size(), 1U);
    EXPECT_EQ(source->buffers.front().source_path,
      std::filesystem::path("authored/raw/vertices.bin"));
    ASSERT_EQ(source->lods.size(), 1U);
    const auto& lod = source->lods.front();
    const auto* buffers = std::get_if<GeometrySource::Buffers>(&lod.mesh);
    ASSERT_NE(buffers, nullptr);
    EXPECT_EQ(buffers->vertex, source->buffers.front().source_id);
    EXPECT_EQ(buffers->index, "/Shared/Buffers/indices.obuf");
    ASSERT_EQ(lod.submeshes.size(), 1U);
    const auto& submesh = lod.submeshes.front();
    EXPECT_EQ(submesh.name, "submesh_0");
    EXPECT_EQ(submesh.material, "/Content/Materials/stone.omat");
    EXPECT_EQ(submesh.bounds.min, lod.bounds.min);
    ASSERT_EQ(submesh.views.size(), 1U);
    EXPECT_EQ(submesh.views.front().view_ref, "surface");
    // A view without authored bounds takes its submesh bounds.
    EXPECT_EQ(submesh.views.front().bounds.min, submesh.bounds.min);
    EXPECT_EQ(submesh.views.front().bounds.max, submesh.bounds.max);
  }

  NOLINT_TEST(GeometrySourceTest, RetainsAuthoredViewBounds)
  {
    auto document = MakeDescriptor();
    document.at("lods")
      .at(0)
      .at("submeshes")
      .at(0)
      .at("views")
      .at(0)
      .emplace(
        "bounds", nlohmann::json::parse(R"({"min":[0,0,0],"max":[1,0.5,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    const auto& view = source->lods.front().submeshes.front().views.front();
    EXPECT_EQ(view.bounds.min, (std::array { 0.0F, 0.0F, 0.0F }));
    EXPECT_EQ(view.bounds.max, (std::array { 1.0F, 0.5F, 1.0F }));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsViewBoundsOutsideTheSubmesh)
  {
    auto document = MakeDescriptor();
    document.at("lods")
      .at(0)
      .at("submeshes")
      .at(0)
      .at("views")
      .at(0)
      .emplace(
        "bounds", nlohmann::json::parse(R"({"min":[0,0,0],"max":[2,1,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.view_bounds_invalid";
      }));
  }

  NOLINT_TEST(GeometrySourceTest, RetainsAllSkinningReferences)
  {
    auto document = MakeDescriptor();
    auto& lod = document.at("lods").at(0);
    lod.at("mesh_type") = "skinned";
    lod.emplace("skinning",
      nlohmann::json {
        { "joint_index_ref", "/Content/Buffers/joints.obuf" },
        { "joint_weight_ref", "/Content/Buffers/weights.obuf" },
        { "inverse_bind_ref", "/Content/Buffers/bind.obuf" },
        { "joint_remap_ref", "/Content/Buffers/remap.obuf" },
        { "skeleton_ref", "/Content/Skeletons/body.oskel" },
        { "joint_count", 4 },
        { "influences_per_vertex", 4 },
        { "flags", 1 },
      });
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    ASSERT_EQ(source->lods.size(), 1U);
    const auto* skin
      = std::get_if<GeometrySource::Skinned>(&source->lods.front().mesh);
    ASSERT_NE(skin, nullptr);
    EXPECT_EQ(skin->buffers.vertex, "/Content/Buffers/vertices.obuf");
    EXPECT_EQ(skin->joint_index, "/Content/Buffers/joints.obuf");
    EXPECT_EQ(skin->joint_weight, "/Content/Buffers/weights.obuf");
    EXPECT_EQ(skin->inverse_bind, "/Content/Buffers/bind.obuf");
    EXPECT_EQ(skin->joint_remap, "/Content/Buffers/remap.obuf");
    EXPECT_EQ(skin->skeleton, "/Content/Skeletons/body.oskel");
    EXPECT_EQ(skin->joint_count, 4U);
    EXPECT_EQ(skin->influences_per_vertex, 4U);
    EXPECT_EQ(skin->flags, 1U);
  }

  NOLINT_TEST(GeometrySourceTest, PreparesProceduralRecipeWithoutGeneratingMesh)
  {
    auto document = MakeDescriptor();
    document.erase("buffers");
    auto& lod = document.at("lods").at(0);
    lod.erase("buffers");
    lod.at("mesh_type") = "procedural";
    lod.emplace("procedural",
      nlohmann::json {
        { "generator", "Sphere" },
        { "mesh_name", "LargeSphere" },
        {
          "params",
          {
            { "latitude_segments", kHugeSegmentCount },
            { "longitude_segments", kHugeSegmentCount },
          },
        },
      });
    lod.at("submeshes").at(0).at("views").at(0).at("view_ref") = "__all__";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    ASSERT_EQ(source->lods.size(), 1U);
    const auto* procedural
      = std::get_if<GeometrySource::Procedural>(&source->lods.front().mesh);
    ASSERT_NE(procedural, nullptr);
    EXPECT_EQ(procedural->name, "Sphere/LargeSphere");
    EXPECT_EQ(procedural->parameters.size(), 8U);
  }

  NOLINT_TEST(GeometrySourceTest, RejectsNilMaterialSlotBeforeResolvingAssets)
  {
    auto document = MakeDescriptor();
    document.at("lods").at(0).at("submeshes").at(0).at("slot_id")
      = "00000000-0000-0000-0000-000000000000";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.slot_id_invalid";
      }));
  }
} // namespace
} // namespace oxygen::content::import::test
