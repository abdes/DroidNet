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

#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>

#include "AsyncImporterFullTestBase.h"

namespace {

using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using oxygen::content::import::test::AsyncImporterFullTestBase;

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
NOLINT_TEST_F(AsyncFbxImporterFullTest, AsyncBackend_ImportsFullDinoScene)
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

  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

//! Async import succeeds for Sponza when asset is available.
/*!
 Validates the async FBX importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncFbxImporterFullTest, DISABLED_AsyncBackend_ImportsSponza)
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
