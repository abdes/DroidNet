//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/Import/Naming.h>

namespace {

using oxygen::content::import::AsyncImportService;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportJobId;
using oxygen::content::import::ImportReport;
using oxygen::content::import::ImportRequest;
using oxygen::content::import::LooseCookedLayout;
using oxygen::content::import::NormalizeNamingStrategy;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

class AsyncGltfImporterFullTest : public ::testing::Test {
protected:
  [[nodiscard]] static auto MakeTempDir(std::string_view suffix)
    -> std::filesystem::path
  {
    const auto root
      = std::filesystem::temp_directory_path() / "oxgn-cntt-tests";
    const auto out_dir = root / std::filesystem::path(std::string(suffix));

    std::error_code ec;
    std::filesystem::remove_all(out_dir, ec);
    std::filesystem::create_directories(out_dir);

    return out_dir;
  }
};

[[nodiscard]] auto MakeMaxConcurrencyConfig() -> AsyncImportService::Config
{
  constexpr uint32_t kVirtualCores = 32U;
  const auto total_workers = kVirtualCores;

  auto fraction_workers = [&](const uint32_t percent) -> uint32_t {
    const auto count = (total_workers * percent) / 100U;
    return std::max(1U, count);
  };

  AsyncImportService::Config config {
    .thread_pool_size = total_workers,
    .max_in_flight_jobs = total_workers,
  };
  config.concurrency = {
    .texture = {
      .workers = fraction_workers(40),
      .queue_capacity = 64,
    },
    .buffer = {
      .workers = fraction_workers(20),
      .queue_capacity = 64,
    },
    .material = {
      .workers = fraction_workers(20),
      .queue_capacity = 64,
    },
    .geometry = {
      .workers = fraction_workers(20),
      .queue_capacity = 32,
    },
    .scene = {
      .workers = 1U,
      .queue_capacity = 8,
    },
  };
  return config;
}

//! Async import succeeds for glTF Sponza when asset is available.
/*!
 Validates the async glTF importer can handle the external-texture Sponza
 dataset when the source file is present on disk.
*/
NOLINT_TEST_F(AsyncGltfImporterFullTest, AsyncBackend_ImportsSponza)
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

  AsyncImportService service(MakeMaxConcurrencyConfig());
  std::latch done(1);
  ImportReport report;
  ImportJobId finished_id = oxygen::content::import::kInvalidJobId;

  // Act
  const auto import_start = steady_clock::now();
  const auto job_id = service.SubmitImport(
    std::move(request), [&](ImportJobId id, const ImportReport& completed) {
      finished_id = id;
      report = completed;
      done.count_down();
    });

  ASSERT_NE(job_id, oxygen::content::import::kInvalidJobId);
  done.wait();
  const auto import_end = steady_clock::now();
  const auto import_ms
    = duration_cast<milliseconds>(import_end - import_start).count();
  GTEST_LOG_(INFO) << "Async glTF import duration: " << import_ms << " ms";

  // Assert
  EXPECT_EQ(finished_id, job_id);
  EXPECT_TRUE(report.success);
  GTEST_LOG_(INFO) << "Cooked root: " << report.cooked_root.string();
}

} // namespace
