//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace oxygen::content::import::tool {

struct ActiveJobView {
  std::string id;
  std::string source;
  std::string status;
  std::string item_kind;
  std::string item_name;
  std::string item_event;
  uint32_t items_completed = 0U;
  uint32_t items_total = 0U;
  float progress = 0.0f;
};

struct WorkerUtilizationView {
  std::string kind;
  uint32_t completed = 0U;
  uint32_t total = 0U;
  uint32_t active = 0U;
  float queue_load = 0.0f;
};

struct BatchViewModel {
  std::string manifest_path;
  size_t completed = 0;
  size_t total = 0;
  size_t in_flight = 0;
  size_t remaining = 0;
  size_t failures = 0;
  std::chrono::seconds elapsed { 0 };
  bool completed_run = false;

  std::vector<ActiveJobView> active_jobs;
  std::vector<WorkerUtilizationView> worker_utilization;
  std::vector<std::string> recent_logs;
  float progress = 0.0f;
};

} // namespace oxygen::content::import::tool
