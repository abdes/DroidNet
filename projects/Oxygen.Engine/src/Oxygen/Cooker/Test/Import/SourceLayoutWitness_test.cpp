//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include <glm/ext/vector_float3.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Test/Import/AsyncImporterFullTestBase.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::content::import;

struct SourceCapture {
  std::shared_ptr<const MaterialSlotProvenance> provenance;
  glm::vec3 bounds_max {};
};

class SourceLayoutWitnessTest : public test::AsyncImporterFullTestBase {
protected:
  static auto FixturePath(const std::string& extension) -> std::filesystem::path
  {
    return std::filesystem::path(__FILE__).parent_path() / "Models"
      / ("static_scalar_triangle." + extension);
  }

  static auto ChangedSourcePath(const std::string& extension)
    -> std::filesystem::path
  {
    return MakeTempDir("source_witness_edit_" + TestName())
      / ("static_scalar_triangle." + extension);
  }

  auto Capture(const std::filesystem::path& source,
    const CoordinateConversionPolicy& coordinates) -> SourceCapture
  {
    auto request = ImportRequest {};
    request.source_path = source;
    request.cooked_root = MakeTempDir("source_witness_cook_" + TestName() + "_"
      + std::to_string(capture_index_++));
    request.options.coordinate = coordinates;
    request.options.naming_strategy = std::make_shared<NoOpNamingStrategy>();
    const auto imported = RunImport(std::move(request));
    EXPECT_TRUE(imported.report.success);
    if (!imported.report.success) {
      for (const auto& diagnostic : imported.report.diagnostics) {
        ADD_FAILURE() << diagnostic.code << ": " << diagnostic.message;
      }
      return {};
    }
    const auto inspection = LoadInspection(imported.report.cooked_root);
    const auto geometry_entry
      = FindAssetOfType(inspection, oxygen::data::AssetType::kGeometry);
    EXPECT_TRUE(geometry_entry.has_value());
    if (!geometry_entry) {
      return {};
    }
    oxygen::serio::FileStream<> stream(
      imported.report.cooked_root / geometry_entry->descriptor_relpath,
      std::ios::in);
    oxygen::serio::Reader reader(stream);
    const oxygen::content::LoaderContext context {
      .current_asset_key = geometry_entry->key,
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    };
    const auto geometry = oxygen::content::loaders::LoadGeometryAsset(context);
    return {
      .provenance = source_provenance_,
      .bounds_max = geometry->BoundingBoxMax(),
    };
  }

  auto VerifyProducerCoordinates(const std::string& extension) -> void
  {
    auto coordinates = CoordinateConversionPolicy {};
    coordinates.bake_transforms_into_meshes = false;
    const auto original = Capture(FixturePath(extension), coordinates);
    ASSERT_NE(original.provenance, nullptr);
    ASSERT_EQ(original.provenance->Geometries().size(), 1U);
    const auto& source = original.provenance->Geometries().front();
    ASSERT_FALSE(oxygen::base::IsAllZero(source.source_layout_witness));
    for (const auto policy : {
           UnitNormalizationPolicy::kPreserveSource,
           UnitNormalizationPolicy::kApplyCustomFactor,
         }) {
      coordinates.unit_normalization = policy;
      coordinates.unit_scale = 10.0F;
      const auto converted = Capture(FixturePath(extension), coordinates);
      ASSERT_NE(converted.provenance, nullptr);
      ASSERT_EQ(converted.provenance->Geometries().size(), 1U);
      const auto& candidate = converted.provenance->Geometries().front();
      EXPECT_EQ(
        candidate.source_geometry_anchor, source.source_geometry_anchor);
      EXPECT_EQ(candidate.source_layout_witness, source.source_layout_witness);
      EXPECT_EQ(candidate.allocations, source.allocations);
      if (policy == UnitNormalizationPolicy::kApplyCustomFactor) {
        EXPECT_NE(converted.bounds_max, original.bounds_max);
      }
    }
    coordinates.bake_transforms_into_meshes = true;
    const auto baked = Capture(FixturePath(extension), coordinates);
    ASSERT_NE(baked.provenance, nullptr);
    ASSERT_FALSE(baked.provenance->Geometries().empty());
    for (const auto& variant : baked.provenance->Geometries()) {
      EXPECT_EQ(variant.source_layout_witness, source.source_layout_witness);
    }
  }

private:
  static auto TestName() -> std::string
  {
    return testing::UnitTest::GetInstance()->current_test_info()->name();
  }

  size_t capture_index_ = 0;
};

NOLINT_TEST_F(
  SourceLayoutWitnessTest, GltfCoordinatesAndBakePolicyPreserveRawWitness)
{
  VerifyProducerCoordinates("gltf");
}

NOLINT_TEST_F(
  SourceLayoutWitnessTest, FbxCoordinatesAndBakePolicyPreserveRawWitness)
{
  VerifyProducerCoordinates("fbx");
}

NOLINT_TEST_F(SourceLayoutWitnessTest,
  FbxPositionAndPolygonConnectivityChangesChangeWitness)
{
  std::ifstream input(FixturePath("fbx"));
  const std::string source { std::istreambuf_iterator<char> { input },
    std::istreambuf_iterator<char> {} };
  ASSERT_FALSE(source.empty());
  const auto coordinates = CoordinateConversionPolicy {};
  const auto original = Capture(FixturePath("fbx"), coordinates);
  ASSERT_NE(original.provenance, nullptr);
  ASSERT_EQ(original.provenance->Geometries().size(), 1U);
  const auto witness
    = original.provenance->Geometries().front().source_layout_witness;
  for (const auto& [before, after] : {
         std::pair { "a: 0,0,0,1,0,0,0,1,0", "a: 0,0,0,2,0,0,0,1,0" },
         std::pair { "a: 0,1,-3", "a: 1,0,-3" },
       }) {
    auto changed = source;
    const auto offset = changed.find(before);
    ASSERT_NE(offset, std::string::npos);
    changed.replace(offset, std::string(before).size(), after);
    const auto path = ChangedSourcePath("fbx");
    {
      std::ofstream output(path);
      output << changed;
      ASSERT_TRUE(output.good());
    }
    const auto captured = Capture(path, coordinates);
    ASSERT_NE(captured.provenance, nullptr);
    ASSERT_EQ(captured.provenance->Geometries().size(), 1U);
    EXPECT_NE(
      captured.provenance->Geometries().front().source_layout_witness, witness);
  }
}

NOLINT_TEST_F(
  SourceLayoutWitnessTest, GltfPrimitiveAdditionChangesSourceWitness)
{
  std::ifstream input(FixturePath("gltf"));
  auto source = nlohmann::json::parse(input);
  const auto coordinates = CoordinateConversionPolicy {};
  const auto original = Capture(FixturePath("gltf"), coordinates);
  ASSERT_NE(original.provenance, nullptr);
  ASSERT_EQ(original.provenance->Geometries().size(), 1U);
  auto primitive = source.at("meshes").at(0).at("primitives").at(0);
  source.at("meshes").at(0).at("primitives").push_back(std::move(primitive));
  const auto path = ChangedSourcePath("gltf");
  {
    std::ofstream output(path);
    output << source.dump();
    ASSERT_TRUE(output.good());
  }
  const auto captured = Capture(path, coordinates);
  ASSERT_NE(captured.provenance, nullptr);
  ASSERT_EQ(captured.provenance->Geometries().size(), 1U);
  EXPECT_NE(captured.provenance->Geometries().front().source_layout_witness,
    original.provenance->Geometries().front().source_layout_witness);
}

NOLINT_TEST_F(
  SourceLayoutWitnessTest, GltfUnsupportedPrimitiveModeDoesNotReplaceProvenance)
{
  const auto original
    = Capture(FixturePath("gltf"), CoordinateConversionPolicy {});
  ASSERT_NE(original.provenance, nullptr);
  std::ifstream input(FixturePath("gltf"));
  auto source = nlohmann::json::parse(input);
  source.at("meshes").at(0).at("primitives").at(0).update({ { "mode", 0 } });
  const auto path = ChangedSourcePath("gltf");
  {
    std::ofstream output(path);
    output << source.dump();
    ASSERT_TRUE(output.good());
  }
  auto request = ImportRequest {};
  request.source_path = path;
  request.cooked_root = MakeTempDir("source_witness_rejected_mode");
  const auto failed = RunImport(std::move(request));
  EXPECT_FALSE(failed.report.success);
  EXPECT_TRUE(failed.report.material_slot_provenance_json.empty());
  EXPECT_EQ(source_provenance_->Serialize(), original.provenance->Serialize());
}

} // namespace
