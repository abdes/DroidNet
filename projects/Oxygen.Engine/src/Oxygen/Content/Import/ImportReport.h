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
#include <vector>

#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportProgress.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::import {

//! Timing telemetry captured during an import job.
struct ImportTelemetry final {
  //! Time spent reading source bytes from storage.
  std::optional<std::chrono::microseconds> io_duration;

  //! Time spent decoding or transforming source bytes in-memory.
  std::optional<std::chrono::microseconds> decode_duration;

  //! Time spent loading or preparing source data.
  std::optional<std::chrono::microseconds> load_duration;

  //! Time spent cooking or processing content.
  std::optional<std::chrono::microseconds> cook_duration;

  //! Time spent emitting cooked outputs.
  std::optional<std::chrono::microseconds> emit_duration;

  //! Time spent finalizing outputs and session state.
  std::optional<std::chrono::microseconds> finalize_duration;

  //! Total wall-clock duration for the job.
  std::optional<std::chrono::microseconds> total_duration;
};

//! Summary of an import to a cooked container.
struct ImportReport final {
  std::filesystem::path cooked_root;
  data::SourceKey source_key { std::array<uint8_t, 16> {} };

  //! Diagnostics (warnings/errors) emitted during import.
  std::vector<ImportDiagnostic> diagnostics;

  //! Count of assets written (by type) for quick UI.
  uint32_t materials_written = 0;
  uint32_t geometry_written = 0;
  uint32_t scenes_written = 0;

  //! Timing and performance telemetry for the job.
  ImportTelemetry telemetry;

  //! True if the cook completed and emitted an index.
  bool success = false;
};

//! Completion callback invoked when import finishes.
using ImportCompletionCallback
  = std::function<void(ImportJobId, const ImportReport&)>;

} // namespace oxygen::content::import
