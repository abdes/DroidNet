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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

using content::lc::Inspection;
using data::AssetType;
using data::ComponentType;
using data::loose_cooked::FileKind;
using data::pak::core::TextureResourceDesc;
using data::pak::world::DirectionalLightRecord;
using data::pak::world::NodeRecord;
using data::pak::world::PointLightRecord;
using data::pak::world::RenderableRecord;
using data::pak::world::SceneAssetDesc;
using data::pak::world::SceneComponentTableDesc;
using data::pak::world::SpotLightRecord;
using import::AsyncImportService;
using import::ImportJobId;
using import::ImportReport;
using import::ImportRequest;
using serio::FileStream;
using serio::Reader;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;

class ModelImportTestBase : public oxygen::cooker::test::TempDirTest {
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

  struct SceneReadback {
    SceneAssetDesc desc {};
    std::vector<NodeRecord> nodes;
    std::vector<SceneComponentTableDesc> component_entries;
    std::vector<RenderableRecord> renderables;
    std::vector<DirectionalLightRecord> directional_lights;
    std::vector<PointLightRecord> point_lights;
    std::vector<SpotLightRecord> spot_lights;
  };

  [[nodiscard]] auto TestModelsDirFromFile() -> std::filesystem::path
  {
    return (oxygen::cooker::test::AssetsDir() / "Models").lexically_normal();
  }

  //! Creates and returns `TempDir() / suffix`; removed with the fixture.
  [[nodiscard]] auto MakeTempDir(std::string_view suffix) const
    -> std::filesystem::path
  {
    auto out_dir = TempPath(suffix);
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

  // Each fixture owns one logical source across repeat imports and copied
  // paths.
  std::shared_ptr<const MaterialSlotProvenance> source_provenance_
    = std::make_shared<const MaterialSlotProvenance>(Uuid::Generate());

  [[nodiscard]] auto RunImport(ImportRequest request) -> ImportRunResult
  {
    if (!request.material_slot_provenance) {
      request.material_slot_provenance = source_provenance_;
    }
    AsyncImportService service(MakeMaxConcurrencyConfig());
    std::latch done(1);
    ImportRunResult result {};

    const auto import_start = steady_clock::now();
    auto job_id_opt = service.SubmitImport(std::move(request),
      [&](ImportJobId id, const ImportReport& completed) -> void {
        result.finished_id = id;
        result.report = completed;
        done.count_down();
      });

    if (!job_id_opt) {
      ADD_FAILURE() << "Import submission was rejected";
      service.Stop();
      return result;
    }
    result.job_id = *job_id_opt;
    EXPECT_NE(result.job_id, kInvalidJobId);
    done.wait();
    const auto import_end = steady_clock::now();
    const auto import_ms
      = duration_cast<milliseconds>(import_end - import_start).count();
    GTEST_LOG_(INFO) << "Async import duration: " << import_ms << " ms";

    service.Stop();

    if (result.report.success
      && !result.report.material_slot_provenance_json.empty()) {
      source_provenance_ = MaterialSlotProvenance::Parse(
        result.report.material_slot_provenance_json);
    }

    return result;
  }

  [[nodiscard]] static auto LoadInspection(const std::filesystem::path& root)
    -> Inspection;

  [[nodiscard]] static auto FindAssetOfType(const Inspection& inspection,
    AssetType type) -> std::optional<Inspection::AssetEntry>;

  [[nodiscard]] static auto CountAssetsOfType(
    const Inspection& inspection, AssetType type) -> size_t;

  [[nodiscard]] static auto LoadSceneReadback(const ImportReport& report)
    -> SceneReadback;

  static auto ValidateSceneOutputs(
    const ImportReport& report, const ExpectedSceneOutputs& expected) -> void;
};

} // namespace oxygen::content::import::test
