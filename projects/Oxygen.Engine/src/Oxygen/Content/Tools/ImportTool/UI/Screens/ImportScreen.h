//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <mutex>

#include <Oxygen/Content/Tools/ImportTool/UI/JobViewModel.h>

namespace oxygen::content::import::tool {

class ImportScreen final {
public:
  using DataProvider = std::function<JobViewModel()>;

  ImportScreen();

  void SetDataProvider(DataProvider provider);

  void Run();

private:
  auto GetStateSnapshot() const -> JobViewModel;
  void UpdateState(JobViewModel state);

  DataProvider provider_;
  mutable std::mutex state_mutex_;
  JobViewModel state_;
  std::atomic<bool> completed_ { false };
  std::atomic<bool> completed_signaled_ { false };
};

} // namespace oxygen::content::import::tool
