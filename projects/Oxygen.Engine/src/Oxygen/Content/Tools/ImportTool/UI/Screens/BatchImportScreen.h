//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <mutex>

#include <Oxygen/Content/Tools/ImportTool/UI/BatchViewModel.h>

namespace oxygen::content::import::tool {

//! Batch import UI rendered with ftxui.
class BatchImportScreen final {
public:
  using DataProvider = std::function<BatchViewModel()>;
  using CompletionCallback = std::function<void()>;

  BatchImportScreen();

  void SetDataProvider(DataProvider provider);

  void SetOnCompleted(CompletionCallback callback);

  void Run();

private:
  auto GetStateSnapshot() const -> BatchViewModel;
  void UpdateState(BatchViewModel state);

  DataProvider provider_;
  CompletionCallback on_completed_;
  mutable std::mutex state_mutex_;
  BatchViewModel state_;
  std::atomic<bool> completed_ { false };
  std::atomic<bool> completed_signaled_ { false };
};

} // namespace oxygen::content::import::tool
