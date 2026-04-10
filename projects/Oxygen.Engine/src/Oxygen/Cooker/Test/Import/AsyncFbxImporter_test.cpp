//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Data/PakFormat_world.h>

#include "AsyncImporterFullTestBase.h"

namespace {

using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::content::import::test::AsyncImporterFullTestBase;
namespace world = oxygen::data::pak::world;

class AsyncFbxImporterFullTest : public AsyncImporterFullTestBase { };

//! Full async import validates supported FBX content is emitted.
/*!
 Uses the async FBX import job to process dino-a.fbx and verifies the cooked
 outputs contain the supported content types.

 Expectations derived from Python MCP analysis of the FBX source:
 - 1 mesh geometry
 - 7 materials
 - 89 scene nodes (Model entries)
 - 2 unique texture files referenced
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, AsyncBackendImportsFullDinoScene)
{
  // Arrange
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "dino-a.fbx";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_fbx_dino");
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

  constexpr size_t kExpectedMaterials = 7u;
  constexpr size_t kExpectedGeometry = 1u;
  constexpr size_t kExpectedScenes = 1u;
  constexpr size_t kExpectedNodesMin = 89u;
  constexpr size_t kExpectedTextureFiles = 2u;

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
    const auto node_flags = scene.nodes[renderable.node_index].node_flags;
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
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

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

//! Async import succeeds for Sponza when asset is available.
/*!
 Validates the async FBX importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, DISABLEDAsyncBackendImportsSponza)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_Zup_003.fbx");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_fbx_sponza");
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

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);
  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

} // namespace
