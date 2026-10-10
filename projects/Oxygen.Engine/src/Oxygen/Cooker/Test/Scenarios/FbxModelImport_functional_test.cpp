//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/AsyncImportService.cpp, Import/Internal/fbx/FbxAdapter.cpp

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/ModelImportTestBase.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::content::import::test::ModelImportTestBase;
namespace world = oxygen::data::pak::world;

class AsyncFbxImporterFullTest : public ModelImportTestBase { };

//! Full async import validates supported FBX content is emitted.
/*!
 Uses the async FBX import job to process static_textured_triangle.fbx and
 verifies the cooked outputs contain the supported content types.

 Expectations derived from the fixture source:
 - 1 mesh geometry (`Model::Triangle`)
 - 1 material
 - at least 1 scene node
 - 1 texture file referenced (`static_textured_checker.png`)
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, ImportsTexturedFbxScene)
{
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "static_textured_triangle.fbx";
  ASSERT_TRUE(std::filesystem::exists(source_path)) << source_path.string();

  const auto temp_dir = MakeTempDir("async_fbx_textured");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  constexpr size_t kExpectedMaterials = 1U;
  constexpr size_t kExpectedGeometry = 1U;
  constexpr size_t kExpectedScenes = 1U;
  constexpr size_t kExpectedNodesMin = 1U;
  // The texture table always starts with the fallback texture at index 0, so
  // the one checker texture is entry 1.
  constexpr size_t kExpectedTextureFiles = 2U;

  const auto run_result = RunImport(std::move(request));

  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const ExpectedSceneOutputs expected {
    .materials = kExpectedMaterials,
    .geometry = kExpectedGeometry,
    .scenes = kExpectedScenes,
    .nodes_min = kExpectedNodesMin,
    .texture_files = kExpectedTextureFiles,
  };
  ValidateSceneOutputs(run_result.report, expected);

  const auto scene = LoadSceneReadback(run_result.report);
  ASSERT_FALSE(scene.renderables.empty());
  for (const auto& renderable : scene.renderables) {
    ASSERT_LT(renderable.node_index, scene.nodes.size());
    const auto node_flags = scene.nodes.at(renderable.node_index).node_flags;
    EXPECT_NE(node_flags & world::kSceneNodeFlag_CastsShadows, 0U);
    EXPECT_NE(node_flags & world::kSceneNodeFlag_ReceivesShadows, 0U);
  }

  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

NOLINT_TEST_F(AsyncFbxImporterFullTest,
  AsyncBackendImportsLightCustomPropertiesAsSceneSemantics)
{
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "light_overrides.fbx";
  ASSERT_TRUE(std::filesystem::exists(source_path)) << source_path.string();

  const auto temp_dir = MakeTempDir("async_fbx_light_overrides");
  ImportRequest request {
    .source_path = source_path,
    .additional_sources = {},
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  const auto run_result = RunImport(std::move(request));

  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const auto scene = LoadSceneReadback(run_result.report);
  EXPECT_TRUE(scene.renderables.empty());
  EXPECT_TRUE(scene.directional_lights.empty());
  ASSERT_EQ(scene.point_lights.size(), 1U);
  EXPECT_TRUE(scene.spot_lights.empty());

  const auto& light = scene.point_lights.front();
  EXPECT_EQ(light.common.affects_world, 0U);
  EXPECT_EQ(light.common.casts_shadows, 0U);
}

} // namespace
