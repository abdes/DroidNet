//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! Timing telemetry captured for a single work item.
struct ImportWorkItemTelemetry final {
  //! Time spent reading bytes from storage for this item.
  std::optional<std::chrono::microseconds> io_duration;

  //! Time spent decoding or transforming bytes in-memory for this item.
  std::optional<std::chrono::microseconds> decode_duration;

  //! Time spent loading or preparing data for this item.
  std::optional<std::chrono::microseconds> load_duration;

  //! Time spent executing pipeline work for this item.
  std::optional<std::chrono::microseconds> cook_duration;

  //! Time spent emitting outputs for this item.
  std::optional<std::chrono::microseconds> emit_duration;
};

//! Timing telemetry captured during an import job.
struct ImportTelemetry final {
  //! Time spent reading source bytes from storage.
  //! Includes source files and external dependencies.
  std::optional<std::chrono::microseconds> io_duration;

  //! Time spent loading the primary source file (IO + parse).
  std::optional<std::chrono::microseconds> source_load_duration;

  //! Total time spent decoding or transforming bytes in-memory.
  //! Aggregates all assets/resources (embedded or external) and excludes
  //! I/O, emission, and LOD building.
  std::optional<std::chrono::microseconds> decode_duration;

  //! Total time spent loading or preparing data.
  //! Includes source load and all asset/resource load steps.
  std::optional<std::chrono::microseconds> load_duration;

  //! Time spent executing pipeline work that cooks content.
  //! Excludes I/O, decode, and emission. Includes LOD building.
  std::optional<std::chrono::microseconds> cook_duration;

  //! Total time spent emitting cooked outputs.
  //! Aggregates all assets/resources emitted during the job.
  std::optional<std::chrono::microseconds> emit_duration;

  //! Time spent in the finalization stage (index/report updates,
  //! session teardown). Not a per-asset/resource aggregate.
  std::optional<std::chrono::microseconds> finalize_duration;

  //! Total wall-clock duration for the job.
  std::optional<std::chrono::microseconds> total_duration;
};

//! Summary of a cooked output produced by an import job.
struct ImportOutputRecord final {
  //! Container-relative path to the cooked output.
  std::string path;

  //! Size of the output in bytes.
  uint64_t size_bytes = 0;
};

//! CI-grade packaging diagnostics summary for import outcomes.
struct ImportPackagingSummary final {
  //! Number of produced output records in the report.
  uint32_t outputs_written = 0;

  //! True when `container.index.bin` was written by this finalize call.
  bool index_written = false;

  //! True when index write was deferred because another session is active.
  bool index_write_deferred = false;

  //! Diagnostics counts by severity.
  uint32_t diagnostics_info = 0;
  uint32_t diagnostics_warning = 0;
  uint32_t diagnostics_error = 0;

  //! Dedup collision diagnostics emitted by resource emitters.
  uint32_t texture_dedup_collisions = 0;
  uint32_t buffer_dedup_collisions = 0;

  //! Loose cooked index collision decisions at packaging time.
  uint32_t index_asset_collisions = 0;
  uint32_t index_file_collisions = 0;
  uint32_t index_collisions_kept = 0;
  uint32_t index_collisions_replaced = 0;
  uint32_t index_collisions_rejected = 0;
};

//! Summary of an import to a cooked container.
struct ImportReport final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key {};

  //! Diagnostics (warnings/errors) emitted during import.
  std::vector<ImportDiagnostic> diagnostics;

  //! Count of assets written (by type) for quick UI.
  uint32_t materials_written = 0;
  uint32_t geometry_written = 0;
  uint32_t scenes_written = 0;
  uint32_t scripts_written = 0;

  //! Scripting-sidecar emission counters.
  /*!
   Populated for `kScriptingSidecar` imports from the finalized emitted
   * state.
   For non-sidecar imports, these counters remain zero.
  */
  uint32_t scripting_components_written = 0;
  uint32_t script_slots_written = 0;
  uint32_t script_params_written = 0;

  //! Cooked outputs emitted during the import.
  std::vector<ImportOutputRecord> outputs;

  //! Timing and performance telemetry for the job.
  ImportTelemetry telemetry {};

  //! Machine-readable packaging and diagnostics summary.
  ImportPackagingSummary packaging {};

  //! True if the cook completed and emitted an index.
  bool success = false;
};

//! Completion callback invoked when import finishes.
using ImportCompletionCallback
  = std::function<void(ImportJobId, const ImportReport&)>;

} // namespace oxygen::content::import
