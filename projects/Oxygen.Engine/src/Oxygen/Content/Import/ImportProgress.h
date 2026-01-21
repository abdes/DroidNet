//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportJobId.h>

namespace oxygen::content::import {

//! Current phase of the import process.
enum class ImportPhase : uint8_t {
  kPending, //!< Job queued, not started.
  kLoading, //!< Loading/parsing source data.
  kPlanning, //!< Building the work plan.
  kWorking, //!< Executing the work plan.
  kFinalizing, //!< Finalizing and writing outputs.
  kComplete, //!< Finished.
  kCancelled, //!< Cancelled by user.
  kFailed, //!< Failed with error.
};

//! Kind of progress event emitted by an import job.
enum class ImportProgressEvent : uint8_t {
  kJobStarted, //!< Job execution started.
  kJobFinished, //!< Job execution finished.
  kPhaseStarted, //!< Phase started.
  kPhaseProgress, //!< Phase progress update.
  kPhaseFinished, //!< Phase finished.
  kItemStarted, //!< Work item started.
  kItemFinished, //!< Work item finished.
};

//! Convert an import phase to a string label.
inline auto to_string(ImportPhase phase) -> std::string
{
  switch (phase) {
  case ImportPhase::kPending:
    return "Pending";
  case ImportPhase::kLoading:
    return "Loading";
  case ImportPhase::kPlanning:
    return "Planning";
  case ImportPhase::kWorking:
    return "Working";
  case ImportPhase::kFinalizing:
    return "Finalizing";
  case ImportPhase::kComplete:
    return "Complete";
  case ImportPhase::kCancelled:
    return "Cancelled";
  case ImportPhase::kFailed:
    return "Failed";
  }
  return "Unknown";
}

//! Convert a progress event to a string label.
inline auto to_string(ImportProgressEvent event) -> std::string
{
  switch (event) {
  case ImportProgressEvent::kJobStarted:
    return "job_started";
  case ImportProgressEvent::kJobFinished:
    return "job_finished";
  case ImportProgressEvent::kPhaseStarted:
    return "phase_started";
  case ImportProgressEvent::kPhaseProgress:
    return "phase_progress";
  case ImportProgressEvent::kPhaseFinished:
    return "phase_finished";
  case ImportProgressEvent::kItemStarted:
    return "item_started";
  case ImportProgressEvent::kItemFinished:
    return "item_finished";
  }
  return "unknown";
}

//! Progress update for UI integration.
struct ImportProgress {
  //! Job this progress applies to.
  ImportJobId job_id = kInvalidJobId;

  //! Type of progress event.
  ImportProgressEvent event = ImportProgressEvent::kPhaseProgress;

  //! Current phase of import.
  ImportPhase phase = ImportPhase::kPending;

  //! Progress within current phase (0.0 - 1.0).
  float phase_progress = 0.0f;

  //! Overall progress (0.0 - 1.0).
  float overall_progress = 0.0f;

  //! Human-readable status message.
  std::string message;

  //! Optional work item kind label (e.g., "TextureResource").
  std::string item_kind;

  //! Optional work item name or identifier.
  std::string item_name;

  //! Items processed in current phase.
  uint32_t items_completed = 0;
  uint32_t items_total = 0;

  //! Incremental diagnostics (warnings/errors as they occur).
  std::vector<ImportDiagnostic> new_diagnostics;
};

//! Progress callback for UI updates.
using ImportProgressCallback = std::function<void(const ImportProgress&)>;

} // namespace oxygen::content::import
