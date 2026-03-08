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

class AsyncGltfImporterFullTest : public AsyncImporterFullTestBase { };

//! Full async import validates supported glTF content is emitted.
/*!
 Uses the async glTF import job to process Tabuleiro.glb and validates the
 cooked outputs contain the expected content types.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, AsyncBackendImportsFullTabuleiroScene)
{
  // Arrange
  const auto models_dir = TestModelsDirFromFile();
  const auto source_path = models_dir / "Tabuleiro.glb";
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_tabuleiro");
  ImportRequest request {
    .source_path = source_path,
    .cooked_root = temp_dir,
    .loose_cooked_layout = LooseCookedLayout {},
    .source_key = std::nullopt,
    .options = {},
  };
  request.options.naming_strategy = std::make_shared<NormalizeNamingStrategy>();
  request.options.import_content = ImportContentFlags::kAll;

  // Act
  const auto run_result = RunImport(std::move(request));

  // Assert
  EXPECT_EQ(run_result.finished_id, run_result.job_id);
  EXPECT_TRUE(run_result.report.success);

  const ExpectedSceneOutputs expected {
    .materials = 3u,
    .geometry = 5u,
    .scenes = 1u,
    .texture_files = 0u,
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

//! Async import succeeds for glTF Sponza when asset is available.
/*!
 Validates the async glTF importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, DISABLEDAsyncBackendImportsSponza)
{
  // Arrange
  const auto source_path = std::filesystem::path(
    "F:\\projects\\main_sponza\\NewSponza_Main_glTF_003.gltf");
  if (!std::filesystem::exists(source_path)) {
    GTEST_SKIP() << "Missing test asset: " << source_path.string();
  }

  const auto temp_dir = MakeTempDir("async_gltf_sponza");
  ImportRequest request {
    .source_path = source_path,
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
