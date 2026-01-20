//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::import::test {

using oxygen::content::LooseCookedInspection;
using oxygen::content::import::AsyncImportService;
using oxygen::content::import::ImportJobId;
using oxygen::content::import::ImportReport;
using oxygen::content::import::ImportRequest;
using oxygen::data::AssetType;
using oxygen::data::ComponentType;
using oxygen::data::loose_cooked::v1::FileKind;
using oxygen::data::pak::RenderableRecord;
using oxygen::data::pak::SceneAssetDesc;
using oxygen::data::pak::SceneComponentTableDesc;
using oxygen::data::pak::TextureResourceDesc;
using oxygen::serio::FileStream;
using oxygen::serio::Reader;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

class AsyncImporterFullTestBase : public ::testing::Test {
protected:
  struct ImportRunResult {
    ImportReport report;
    ImportJobId finished_id = kInvalidJobId;
    ImportJobId job_id = kInvalidJobId;
  };

  struct ExpectedSceneOutputs {
    std::optional<size_t> materials;
    std::optional<size_t> geometry;
    std::optional<size_t> scenes;
    std::optional<size_t> nodes_min;
    std::optional<size_t> texture_files;
  };

  [[nodiscard]] auto TestModelsDirFromFile() -> std::filesystem::path
  {
    auto path = std::filesystem::path(__FILE__).parent_path();
    path /= "..";
    path /= "Models";
    return path.lexically_normal();
  }

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

  [[nodiscard]] static auto MakeMaxConcurrencyConfig()
    -> AsyncImportService::Config
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

  [[nodiscard]] static auto RunImport(ImportRequest request) -> ImportRunResult
  {
    AsyncImportService service(MakeMaxConcurrencyConfig());
    std::latch done(1);
    ImportRunResult result {};

    const auto import_start = steady_clock::now();
    result.job_id = service.SubmitImport(
      std::move(request), [&](ImportJobId id, const ImportReport& completed) {
        result.finished_id = id;
        result.report = completed;
        done.count_down();
      });

    EXPECT_NE(result.job_id, kInvalidJobId);
    done.wait();
    const auto import_end = steady_clock::now();
    const auto import_ms
      = duration_cast<milliseconds>(import_end - import_start).count();
    GTEST_LOG_(INFO) << "Async import duration: " << import_ms << " ms";

    service.Stop();

    return result;
  }

  [[nodiscard]] static auto LoadInspection(const std::filesystem::path& root)
    -> LooseCookedInspection;

  [[nodiscard]] static auto FindAssetOfType(
    const LooseCookedInspection& inspection, const AssetType type)
    -> std::optional<LooseCookedInspection::AssetEntry>;

  [[nodiscard]] static auto CountAssetsOfType(
    const LooseCookedInspection& inspection, const AssetType type) -> size_t;

  static auto ValidateSceneOutputs(
    const ImportReport& report, const ExpectedSceneOutputs& expected) -> void;
};

} // namespace oxygen::content::import::test
