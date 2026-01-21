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

class AsyncGltfImporterFullTest : public AsyncImporterFullTestBase { };

//! Full async import validates supported glTF content is emitted.
/*!
 Uses the async glTF import job to process Tabuleiro.glb and validates the
 cooked outputs contain the expected content types.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, AsyncBackend_ImportsFullTabuleiroScene)
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

  GTEST_LOG_(INFO) << "Cooked root: " << run_result.report.cooked_root.string();
}

//! Async import succeeds for glTF Sponza when asset is available.
/*!
 Validates the async glTF importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, DISABLED_AsyncBackend_ImportsSponza)
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
