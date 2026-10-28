//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/GeometrySource.cpp,
// Import/Internal/GeometryDefaults.cpp

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
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
          "surface_id":"018f8f8f-1111-7111-8111-111111111111",
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(source->name, "Geometry.v2");
    ASSERT_HAS_VALUE(source->content_hashing)
      << "Expected source->content_hashing to contain a value";
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
    ASSERT_HAS_VALUE(lod.bounds);
    ASSERT_HAS_VALUE(submesh.bounds);
    // A submesh without authored bounds takes its LOD bounds.
    EXPECT_EQ(submesh.bounds->min, lod.bounds->min);
    EXPECT_EQ(submesh.bounds->max, lod.bounds->max);
    ASSERT_EQ(submesh.views.size(), 1U);
    const auto& view = submesh.views.front();
    EXPECT_EQ(view.view_ref, "surface");
    // A view without authored bounds takes its submesh bounds.
    ASSERT_HAS_VALUE(view.bounds);
    EXPECT_EQ(view.bounds->min, submesh.bounds->min);
    EXPECT_EQ(view.bounds->max, submesh.bounds->max);
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
    ASSERT_HAS_VALUE(view.bounds);
    EXPECT_EQ(view.bounds->min, (std::array { 0.0F, 0.0F, 0.0F }));
    EXPECT_EQ(view.bounds->max, (std::array { 1.0F, 0.5F, 1.0F }));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsSubmeshBoundsOutsideTheLod)
  {
    auto document = MakeDescriptor();
    document.at("lods")
      .at(0)
      .at("submeshes")
      .at(0)
      .emplace(
        "bounds", nlohmann::json::parse(R"({"min":[0,0,0],"max":[2,1,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.submesh_bounds_invalid";
      }));
  }

  //! Absent geometry bounds are left to the cook, which unions the LODs'.
  NOLINT_TEST(GeometrySourceTest, GeometryBoundsAreOptional)
  {
    auto document = MakeDescriptor();
    document.erase("bounds");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_FALSE(source->bounds.has_value());
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
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
    lod.erase("bounds");
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
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    ASSERT_EQ(source->lods.size(), 1U);
    const auto* procedural
      = std::get_if<GeometrySource::Procedural>(&source->lods.front().mesh);
    ASSERT_NE(procedural, nullptr);
    EXPECT_EQ(procedural->name, "Sphere/LargeSphere");
    EXPECT_EQ(procedural->parameters.size(), 8U);
    // Only the generator can bound the mesh, when the geometry is cooked.
    const auto& prepared = source->lods.front();
    EXPECT_FALSE(prepared.bounds.has_value());
    EXPECT_FALSE(prepared.submeshes.front().bounds.has_value());
    EXPECT_FALSE(prepared.submeshes.front().views.front().bounds.has_value());
  }

  NOLINT_TEST(GeometrySourceTest, RejectsNilSurfaceBeforeResolvingAssets)
  {
    auto document = MakeDescriptor();
    document.at("lods").at(0).at("submeshes").at(0).at("surface_id")
      = "00000000-0000-0000-0000-000000000000";
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.surface_id_invalid";
      }));
  }

  //! The descriptor's surface id, the one surface it declares.
  constexpr auto kSurface = std::string_view {
    "018f8f8f-1111-7111-8111-111111111111",
  };

  auto HasCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const auto& diagnostic) -> auto {
        return diagnostic.code == code;
      });
  }

  NOLINT_TEST(GeometrySourceTest, RetainsAuthoredAssetDefaults)
  {
    auto document = MakeDescriptor();
    document.emplace("surfaces",
      nlohmann::json::array({
        {
          { "surface_id", kSurface },
          { "visible", false },
        },
      }));
    document.emplace("lod_policy",
      nlohmann::json::parse(
        R"({"type":"distance","thresholds":[0.5,0.75],"hysteresis_ratio":0.1})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    ASSERT_EQ(source->surfaces.size(), 1U);
    EXPECT_EQ(source->surfaces.front().visible, false);
    EXPECT_FALSE(source->surfaces.front().casts_shadows.has_value());
    ASSERT_HAS_VALUE(source->lod_policy) << "Expected a LOD policy";
    EXPECT_EQ(
      source->lod_policy->type, data::pak::geometry::LodPolicyType::kDistance);
    EXPECT_EQ(source->lod_policy->boundary_count, 2U);
    EXPECT_EQ(source->lod_policy->thresholds[1], 0.75F);
  }

  NOLINT_TEST(GeometrySourceTest, RejectsDefaultsOfUndeclaredSurface)
  {
    auto document = MakeDescriptor();
    document.emplace("surfaces",
      nlohmann::json::array({
        {
          { "surface_id", "018f8f8f-2222-7222-8222-222222222222" },
          { "visible", false },
        },
      }));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.surface_unknown"));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsRepeatedSurfaceDefaults)
  {
    auto document = MakeDescriptor();
    document.emplace("surfaces",
      nlohmann::json::array({
        { { "surface_id", kSurface }, { "visible", false } },
        { { "surface_id", kSurface }, { "casts_shadows", false } },
      }));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.surface_duplicate"));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsDecreasingLodThresholds)
  {
    auto document = MakeDescriptor();
    document.emplace("lod_policy",
      nlohmann::json::parse(
        R"({"type":"distance","thresholds":[0.75,0.5],"hysteresis_ratio":0.1})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.lod_policy_invalid"));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsIncreasingScreenSpaceErrors)
  {
    auto document = MakeDescriptor();
    document.emplace(
      "lod_policy", nlohmann::json::parse(R"({"type":"screen_space_error",
        "enter_finer_sse":[1,2],"exit_coarser_sse":[0.5,1.5]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.lod_policy_invalid"));
  }

  NOLINT_TEST(GeometrySourceTest, RejectsScreenSpaceErrorListsOfDifferentLength)
  {
    auto document = MakeDescriptor();
    document.emplace(
      "lod_policy", nlohmann::json::parse(R"({"type":"screen_space_error",
        "enter_finer_sse":[2,1],"exit_coarser_sse":[1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(
      document.dump(), "geometry.json", diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.lod_policy_invalid"));
  }

  //! Absent values take the scene schema's defaults.
  NOLINT_TEST(GeometryDefaultsTest, SurfaceTableTakesSchemaDefaults)
  {
    using data::pak::geometry::kSurfaceFlag_CastsShadows;
    using data::pak::geometry::kSurfaceFlag_Visible;
    namespace surface_schema = data::schema::geometry::surface;

    const auto first = data::SurfaceId::FromStableIdentity("first");
    const auto second = data::SurfaceId::FromStableIdentity("second");
    const auto declared = std::array { first, second };
    const auto authored = std::array {
      internal::AuthoredSurfaceDefaults {
        .surface_id = second,
        .visible = false,
        .casts_shadows = std::nullopt,
      },
    };
    const auto table = internal::MakeSurfaceTable(declared, authored);

    const auto expected_flags = [](const bool visible, const bool casts) {
      return (visible ? kSurfaceFlag_Visible : 0U)
        | (casts ? kSurfaceFlag_CastsShadows : 0U);
    };
    ASSERT_EQ(table.size(), 2U);
    EXPECT_EQ(table.at(0).surface_id, first);
    EXPECT_EQ(table.at(0).flags,
      expected_flags(surface_schema::kVisibilityDefault,
        surface_schema::kCastsShadowsDefault));
    EXPECT_EQ(table.at(1).surface_id, second);
    EXPECT_EQ(table.at(1).flags,
      expected_flags(false, surface_schema::kCastsShadowsDefault));
  }

  NOLINT_TEST(GeometryDefaultsTest, LodPolicyRoundTripsThroughJson)
  {
    const auto document = nlohmann::json::parse(R"({"type":"screen_space_error",
      "enter_finer_sse":[2,1],"exit_coarser_sse":[1.5,0.5]})");
    const auto record = internal::ReadLodPolicy(document);
    ASSERT_HAS_VALUE(record) << "Expected a LOD policy";
    EXPECT_EQ(internal::WriteLodPolicy(*record), document);
    EXPECT_EQ(internal::MakeDefaultLodPolicy(std::nullopt).type,
      data::schema::geometry::kLodPolicyDefault.type);
  }

  //! An imported geometry with several LODs switches them by screen-space
  //! error, halving the projected radius per LOD; one LOD keeps the schema's
  //! default.
  NOLINT_TEST(GeometryDefaultsTest, ImportedLodPolicySwitchesSeveralLods)
  {
    const auto single = internal::MakeImportedLodPolicy(1);
    const auto three = internal::MakeImportedLodPolicy(3);

    EXPECT_EQ(single.type, data::schema::geometry::kLodPolicyDefault.type);
    EXPECT_EQ(
      three.type, data::pak::geometry::LodPolicyType::kScreenSpaceError);
    ASSERT_EQ(three.boundary_count, 2U);
    EXPECT_THAT(std::span<const float>(three.enter_finer_sse).first(3),
      testing::ElementsAre(256.0F, 128.0F, 0.0F));
    EXPECT_THAT(std::span<const float>(three.exit_coarser_sse).first(3),
      testing::ElementsAre(
        testing::FloatEq(204.8F), testing::FloatEq(102.4F), 0.0F));
    EXPECT_TRUE(data::IsValidLodPolicy(three));
  }

  //! Generated LODs switch where the coarser LOD's error would exceed one
  //! pixel: boundary `i` enters its finer LOD at `radius / e_(i+1)` pixels,
  //! exits at 0.8 of that, and an error below a millionth of the radius counts
  //! as that millionth.
  NOLINT_TEST(GeometryDefaultsTest, GeneratedLodPolicyFollowsTheErrors)
  {
    constexpr auto kRadius = 2.0F;
    const auto errors = std::array { 0.0F, 0.01F, 0.5F };

    const auto policy = internal::MakeGeneratedLodPolicy(errors, kRadius);

    EXPECT_EQ(
      policy.type, data::pak::geometry::LodPolicyType::kScreenSpaceError);
    ASSERT_EQ(policy.boundary_count, 3U);
    EXPECT_THAT(std::span<const float>(policy.enter_finer_sse).first(4),
      testing::ElementsAre(testing::FloatNear(1'000'000.0F, 1.0F),
        testing::FloatEq(200.0F), testing::FloatEq(4.0F), 0.0F));
    EXPECT_THAT(std::span<const float>(policy.exit_coarser_sse).first(4),
      testing::ElementsAre(testing::FloatNear(800'000.0F, 1.0F),
        testing::FloatEq(160.0F), testing::FloatEq(3.2F), 0.0F));
    EXPECT_TRUE(data::IsValidLodPolicy(policy));
    EXPECT_EQ(internal::MakeGeneratedLodPolicy({}, kRadius).type,
      data::schema::geometry::kLodPolicyDefault.type);
  }
} // namespace
} // namespace oxygen::content::import::test
