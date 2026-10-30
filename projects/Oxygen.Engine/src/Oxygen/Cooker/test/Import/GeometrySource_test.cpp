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
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/TempDir.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/LodPolicyValidation.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using internal::GeometrySource;
  using oxygen::cooker::test::ContentMountTable;
  using oxygen::cooker::test::GeometryDescriptor;
  using oxygen::cooker::test::TempDirTest;

  //! The project's `Content` mount is the temporary root.
  class GeometrySourceTest : public TempDirTest {
  protected:
    auto Mounts() -> const MountTable&
    {
      if (!mounts_) {
        mounts_ = ContentMountTable(TempDir());
      }
      return *mounts_;
    }

  private:
    std::shared_ptr<const MountTable> mounts_;
  };

  //! A sphere resolution far beyond any generator limit.
  constexpr auto kHugeSegmentCount = 1'000'000;

  //! A standard-mesh geometry with one LOD, one submesh and one raw buffer.
  auto MakeDescriptor() -> GeometryDescriptor
  {
    auto geometry = GeometryDescriptor { "/Content/Geometry/Geometry.v2.ogeo" };
    geometry.Set("/content_hashing", false)
      .Set(
        "/bounds", nlohmann::json::parse(R"({"min":[-1,-1,-1],"max":[1,1,1]})"))
      .AppendBuffer(
        { { "uri", "raw/vertices.bin" } }, "/Content/Buffers/vertices.obuf")
      .Remove("/lods/0/procedural")
      .Set("/lods/0/mesh_type", "standard")
      .Set("/lods/0/bounds",
        nlohmann::json::parse(R"({"min":[-1,-1,-1],"max":[1,1,1]})"))
      .Set("/lods/0/buffers/vb_ref", "../Buffers/vertices.obuf")
      .Reference("/lods/0/buffers/ib_ref", "/Shared/Buffers/indices.obuf")
      .Set("/lods/0/submeshes/0/material_ref", "../Materials/stone.omat")
      .Set("/lods/0/submeshes/0/views/0/view_ref", "surface");
    return geometry;
  }

  NOLINT_TEST_F(
    GeometrySourceTest, RetainsSourcesAndSymbolicReferencesWithoutCookedRoots)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto document = MakeDescriptor();
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "authored/geometry.json", document.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_TRUE(diagnostics.empty());
    EXPECT_EQ(source->name, "Geometry.v2");
    ASSERT_HAS_VALUE(source->content_hashing)
      << "Expected source->content_hashing to contain a value";
    EXPECT_FALSE(*source->content_hashing);
    ASSERT_EQ(source->buffers.size(), 1U);
    EXPECT_EQ(source->buffers.front().source_path,
      std::filesystem::weakly_canonical(
        TempDir() / "authored/raw/vertices.bin"));
    ASSERT_EQ(source->lods.size(), 1U);
    const auto& lod = source->lods.front();
    const auto* buffers = std::get_if<GeometrySource::Buffers>(&lod.mesh);
    ASSERT_NE(buffers, nullptr);
    // References stay as written; a product is named by its resolved path.
    EXPECT_EQ(buffers->vertex, "../Buffers/vertices.obuf");
    EXPECT_EQ(
      source->buffers.front().source_id, "/Content/Buffers/vertices.obuf");
    EXPECT_EQ(ResolveReference(document.Identity(), buffers->vertex),
      source->buffers.front().source_id);
    EXPECT_EQ(buffers->index, "/Shared/Buffers/indices.obuf");
    ASSERT_EQ(lod.submeshes.size(), 1U);
    const auto& submesh = lod.submeshes.front();
    EXPECT_EQ(submesh.name, "submesh_0");
    EXPECT_EQ(submesh.material, "../Materials/stone.omat");
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

  NOLINT_TEST_F(GeometrySourceTest, RetainsAuthoredViewBounds)
  {
    auto document = MakeDescriptor();
    document.Set("/lods/0/submeshes/0/views/0/bounds",
      nlohmann::json::parse(R"({"min":[0,0,0],"max":[1,0.5,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics);
    if (!source.has_value()) {
      FAIL() << "Expected source to contain a value";
    }
    const auto& view = source->lods.front().submeshes.front().views.front();
    ASSERT_HAS_VALUE(view.bounds);
    EXPECT_EQ(view.bounds->min, (std::array { 0.0F, 0.0F, 0.0F }));
    EXPECT_EQ(view.bounds->max, (std::array { 1.0F, 0.5F, 1.0F }));
  }

  NOLINT_TEST_F(GeometrySourceTest, RejectsSubmeshBoundsOutsideTheLod)
  {
    auto document = MakeDescriptor();
    document.Set("/lods/0/submeshes/0/bounds",
      nlohmann::json::parse(R"({"min":[0,0,0],"max":[2,1,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.submesh_bounds_invalid";
      }));
  }

  //! Absent geometry bounds are left to the cook, which unions the LODs'.
  NOLINT_TEST_F(GeometrySourceTest, GeometryBoundsAreOptional)
  {
    auto document = MakeDescriptor();
    document.Remove("/bounds");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    EXPECT_FALSE(source->bounds.has_value());
  }

  NOLINT_TEST_F(GeometrySourceTest, RejectsViewBoundsOutsideTheSubmesh)
  {
    auto document = MakeDescriptor();
    document.Set("/lods/0/submeshes/0/views/0/bounds",
      nlohmann::json::parse(R"({"min":[0,0,0],"max":[2,1,1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(
      std::ranges::any_of(diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.code == "geometry.descriptor.view_bounds_invalid";
      }));
  }

  NOLINT_TEST_F(GeometrySourceTest, RetainsAllSkinningReferences)
  {
    auto document = MakeDescriptor();
    document.Set("/lods/0/mesh_type", "skinned")
      .Reference(
        "/lods/0/skinning/joint_index_ref", "/Content/Buffers/joints.obuf")
      .Reference(
        "/lods/0/skinning/joint_weight_ref", "/Content/Buffers/weights.obuf")
      .Reference(
        "/lods/0/skinning/inverse_bind_ref", "/Content/Buffers/bind.obuf")
      .Reference(
        "/lods/0/skinning/joint_remap_ref", "/Content/Buffers/remap.obuf")
      .Reference(
        "/lods/0/skinning/skeleton_ref", "/Content/Skeletons/body.oskel")
      .Set("/lods/0/skinning/joint_count", 4)
      .Set("/lods/0/skinning/influences_per_vertex", 4)
      .Set("/lods/0/skinning/flags", 1);
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics);
    ASSERT_HAS_VALUE(source) << "Expected source to contain a value";
    ASSERT_EQ(source->lods.size(), 1U);
    const auto* skin
      = std::get_if<GeometrySource::Skinned>(&source->lods.front().mesh);
    ASSERT_NE(skin, nullptr);
    EXPECT_EQ(skin->buffers.vertex, "../Buffers/vertices.obuf");
    EXPECT_EQ(
      skin->joint_index, document.Spell("/Content/Buffers/joints.obuf"));
    EXPECT_EQ(
      skin->joint_weight, document.Spell("/Content/Buffers/weights.obuf"));
    EXPECT_EQ(skin->inverse_bind, document.Spell("/Content/Buffers/bind.obuf"));
    EXPECT_EQ(skin->joint_remap, document.Spell("/Content/Buffers/remap.obuf"));
    EXPECT_EQ(skin->skeleton, document.Spell("/Content/Skeletons/body.oskel"));
    EXPECT_EQ(skin->joint_count, 4U);
    EXPECT_EQ(skin->influences_per_vertex, 4U);
    EXPECT_EQ(skin->flags, 1U);
  }

  NOLINT_TEST_F(
    GeometrySourceTest, PreparesProceduralRecipeWithoutGeneratingMesh)
  {
    auto document = MakeDescriptor();
    document.Remove("/buffers")
      .Remove("/lods/0/buffers")
      .Remove("/lods/0/bounds")
      .Set("/lods/0/mesh_type", "procedural")
      .Set("/lods/0/procedural",
        {
          { "generator", "Sphere" },
          { "mesh_name", "LargeSphere" },
          {
            "params",
            {
              { "latitude_segments", kHugeSegmentCount },
              { "longitude_segments", kHugeSegmentCount },
            },
          },
        })
      .Set("/lods/0/submeshes/0/views/0/view_ref", "__all__");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics);
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

  NOLINT_TEST_F(GeometrySourceTest, RejectsNilSurfaceBeforeResolvingAssets)
  {
    auto document = MakeDescriptor();
    document.Set(
      "/lods/0/submeshes/0/surface_id", "00000000-0000-0000-0000-000000000000");
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
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

  NOLINT_TEST_F(GeometrySourceTest, RetainsAuthoredAssetDefaults)
  {
    auto document = MakeDescriptor();
    document.Set("/surfaces",
      nlohmann::json::array({
        {
          { "surface_id", kSurface },
          { "visible", false },
        },
      }));
    document.Set("/lod_policy",
      nlohmann::json::parse(
        R"({"type":"distance","thresholds":[0.5,0.75],"hysteresis_ratio":0.1})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto source = GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics);
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

  NOLINT_TEST_F(GeometrySourceTest, RejectsDefaultsOfUndeclaredSurface)
  {
    auto document = MakeDescriptor();
    document.Set("/surfaces",
      nlohmann::json::array({
        {
          { "surface_id", "018f8f8f-2222-7222-8222-222222222222" },
          { "visible", false },
        },
      }));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.surface_unknown"));
  }

  NOLINT_TEST_F(GeometrySourceTest, RejectsRepeatedSurfaceDefaults)
  {
    auto document = MakeDescriptor();
    document.Set("/surfaces",
      nlohmann::json::array({
        { { "surface_id", kSurface }, { "visible", false } },
        { { "surface_id", kSurface }, { "casts_shadows", false } },
      }));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.surface_duplicate"));
  }

  NOLINT_TEST_F(GeometrySourceTest, RejectsDecreasingLodThresholds)
  {
    auto document = MakeDescriptor();
    document.Set("/lod_policy",
      nlohmann::json::parse(
        R"({"type":"distance","thresholds":[0.75,0.5],"hysteresis_ratio":0.1})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.lod_policy_invalid"));
  }

  NOLINT_TEST_F(GeometrySourceTest, RejectsIncreasingScreenSpaceErrors)
  {
    auto document = MakeDescriptor();
    document.Set(
      "/lod_policy", nlohmann::json::parse(R"({"type":"screen_space_error",
        "enter_finer_sse":[1,2],"exit_coarser_sse":[0.5,1.5]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
    EXPECT_TRUE(HasCode(diagnostics, "geometry.descriptor.lod_policy_invalid"));
  }

  NOLINT_TEST_F(
    GeometrySourceTest, RejectsScreenSpaceErrorListsOfDifferentLength)
  {
    auto document = MakeDescriptor();
    document.Set(
      "/lod_policy", nlohmann::json::parse(R"({"type":"screen_space_error",
        "enter_finer_sse":[2,1],"exit_coarser_sse":[1]})"));
    auto diagnostics = std::vector<ImportDiagnostic> {};
    EXPECT_FALSE(GeometrySource::FromDescriptor(document.Text(),
      "geometry.json", document.Identity(), Mounts(), diagnostics));
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
