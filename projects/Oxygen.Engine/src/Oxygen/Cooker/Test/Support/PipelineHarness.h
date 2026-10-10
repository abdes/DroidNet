//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

namespace oxygen::cooker::test {

//! Runs the Start/Submit/Collect/Close sequence of an import pipeline.
/*!
 Constructs `Pipeline` from `ctor_args` inside `co::Run`, opens one nursery,
 submits every item in order, collects exactly `items.size()` results and
 closes the pipeline. `co::Run` blocks until completion, so `ctor_args` (for
 example a fixture-owned `co::ThreadPool`) only need to outlive this call.

 @return The collected results, in the order the pipeline produced them.
*/
template <class Pipeline, class... Args>
auto RunPipeline(content::import::ImportEventLoop& loop,
  std::vector<typename Pipeline::WorkItem> items, Args&&... ctor_args)
  -> std::vector<typename Pipeline::WorkResult>
{
  std::vector<typename Pipeline::WorkResult> results;
  results.reserve(items.size());

  co::Run(loop, [&] -> co::Co<> {
    Pipeline pipeline(std::forward<Args>(ctor_args)...);

    OXCO_WITH_NURSERY(n)
    {
      pipeline.Start(n);
      for (auto& item : items) {
        co_await pipeline.Submit(std::move(item));
      }
      for (std::size_t i = 0; i < items.size(); ++i) {
        results.push_back(co_await pipeline.Collect());
      }
      pipeline.Close();

      co_return co::kJoin;
    };
  });

  return results;
}

//! Runs a single work item through `RunPipeline` and returns its result.
template <class Pipeline, class... Args>
auto RunPipelineOnce(content::import::ImportEventLoop& loop,
  typename Pipeline::WorkItem item, Args&&... ctor_args) -> Pipeline::WorkResult
{
  std::vector<typename Pipeline::WorkItem> items;
  items.push_back(std::move(item));
  auto results = RunPipeline<Pipeline>(
    loop, std::move(items), std::forward<Args>(ctor_args)...);
  return std::move(results.front());
}

} // namespace oxygen::cooker::test
