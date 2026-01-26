//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
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

//! Kind of progress event payload.
enum class ProgressEventKind : uint8_t {
  kJobStarted,
  kJobFinished,
  kPhaseUpdate,
  kItemStarted,
  kItemFinished,
  kItemCollected,
};

//! Convert a progress event kind to a string label.
inline auto to_string(ProgressEventKind kind) -> std::string
{
  switch (kind) {
  case ProgressEventKind::kJobStarted:
    return "job_started";
  case ProgressEventKind::kJobFinished:
    return "job_finished";
  case ProgressEventKind::kPhaseUpdate:
    return "phase_update";
  case ProgressEventKind::kItemStarted:
    return "item_started";
  case ProgressEventKind::kItemFinished:
    return "item_finished";
  case ProgressEventKind::kItemCollected:
    return "item_collected";
  }
  return "unknown";
}

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

//! Shared header for all progress events.
struct ProgressHeader {
  ImportJobId job_id = kInvalidJobId;
  ImportPhase phase = ImportPhase::kPending;
  ProgressEventKind kind = ProgressEventKind::kPhaseUpdate;
  float overall_progress = 0.0f;
  std::string message;
  std::vector<ImportDiagnostic> new_diagnostics;
};

//! Payload for item progress updates.
struct ItemProgress {
  std::string item_kind;
  std::string item_name;
  float input_queue_load = -1.0f;
  float output_queue_load = -1.0f;
};

//! Variant payload for progress events.
using ProgressPayload = std::variant<std::monostate, ItemProgress>;

//! Full progress event with header and payload.
struct ProgressEvent {
  ProgressHeader header;
  ProgressPayload payload;
};

//! Create a phase progress event.
[[nodiscard]] inline auto MakePhaseProgress(ImportJobId job_id,
  ImportPhase phase, float overall_progress, std::string message = {})
  -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kPhaseUpdate,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = std::monostate {};
  return event;
}

//! Create an item started event.
[[nodiscard]] inline auto MakeItemStarted(ImportJobId job_id, ImportPhase phase,
  float overall_progress, std::string item_kind, std::string item_name,
  std::string message = {}) -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kItemStarted,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = ItemProgress {
    .item_kind = std::move(item_kind),
    .item_name = std::move(item_name),
  };
  return event;
}

//! Create an item finished event.
[[nodiscard]] inline auto MakeItemFinished(ImportJobId job_id,
  ImportPhase phase, float overall_progress, std::string item_kind,
  std::string item_name, std::string message = {}) -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kItemFinished,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = ItemProgress {
    .item_kind = std::move(item_kind),
    .item_name = std::move(item_name),
    .input_queue_load = -1.0f,
    .output_queue_load = -1.0f,
  };
  return event;
}

//! Create an item collected event.
[[nodiscard]] inline auto MakeItemCollected(ImportJobId job_id,
  ImportPhase phase, float overall_progress, std::string item_kind,
  std::string item_name, float input_queue_load, float output_queue_load,
  std::string message = {}) -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kItemCollected,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = ItemProgress {
    .item_kind = std::move(item_kind),
    .item_name = std::move(item_name),
    .input_queue_load = input_queue_load,
    .output_queue_load = output_queue_load,
  };
  return event;
}

//! Create a job started event.
[[nodiscard]] inline auto MakeJobStarted(ImportJobId job_id, ImportPhase phase,
  float overall_progress, std::string message = {}) -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kJobStarted,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = std::monostate {};
  return event;
}

//! Create a job finished event.
[[nodiscard]] inline auto MakeJobFinished(ImportJobId job_id, ImportPhase phase,
  float overall_progress, std::string message = {}) -> ProgressEvent
{
  ProgressEvent event {};
  event.header = ProgressHeader {
    .job_id = job_id,
    .phase = phase,
    .kind = ProgressEventKind::kJobFinished,
    .overall_progress = overall_progress,
    .message = std::move(message),
  };
  event.payload = std::monostate {};
  return event;
}

//! Check whether the event is an item update.
[[nodiscard]] inline auto IsItemProgress(const ProgressEvent& event) -> bool
{
  return std::holds_alternative<ItemProgress>(event.payload);
}

//! Get the item payload if available.
[[nodiscard]] inline auto GetItemProgress(const ProgressEvent& event)
  -> const ItemProgress*
{
  return std::get_if<ItemProgress>(&event.payload);
}

//! Progress callback for UI updates.
using ProgressEventCallback = std::function<void(const ProgressEvent&)>;

} // namespace oxygen::content::import
