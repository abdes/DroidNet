//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <exception>
#include <thread>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Test/Mocks/TestImportJob.h>
#include <Oxygen/OxCo/ThreadPool.h>

using namespace std::chrono_literals;
using oxygen::co::ThreadPool;
namespace co = oxygen::co;

namespace oxygen::content::import::test {

TestImportJob::TestImportJob(ImportJobId job_id, ImportRequest request,
  ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
  std::shared_ptr<co::Event> cancel_event,
  oxygen::observer_ptr<IAsyncFileReader> file_reader,
  oxygen::observer_ptr<IAsyncFileWriter> file_writer,
  oxygen::observer_ptr<co::ThreadPool> thread_pool,
  oxygen::observer_ptr<ResourceTableRegistry> table_registry,
  ImportConcurrency concurrency, Config config)
  : detail::ImportJob(job_id, std::move(request), std::move(on_complete),
      std::move(on_progress), std::move(cancel_event), file_reader, file_writer,
      thread_pool, table_registry, concurrency)
  , config_(config)
{
}

auto TestImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  auto make_cancelled_report = [&]() -> ImportReport {
    ImportReport report {
      .cooked_root
      = Request().cooked_root.value_or(Request().source_path.parent_path()),
      .success = false,
    };

    report.diagnostics.push_back({
      .severity = ImportSeverity::kInfo,
      .code = "import.cancelled",
      .message = "Import cancelled",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });

    return report;
  };

  const auto stop_token = StopToken();
  if (stop_token.stop_requested()) {
    co_return make_cancelled_report();
  }

  auto step_delay = config_.step_delay;
  if (step_delay <= 0ms) {
    step_delay = 1ms;
  }

  const auto total_delay = std::max(config_.total_delay, step_delay);
  const auto step_count
    = std::max<int>(1, static_cast<int>(total_delay / step_delay));

  auto thread_pool = ThreadPool();
  if (thread_pool == nullptr) {
    co_return make_cancelled_report();
  }

  for (int step = 0; step < step_count; ++step) {
    if (stop_token.stop_requested()) {
      co_return make_cancelled_report();
    }

    try {
      co_await thread_pool->Run(
        [step_delay](co::ThreadPool::CancelToken cancelled) {
          if (cancelled) {
            return;
          }
          std::this_thread::sleep_for(step_delay);
        });
    } catch (const std::exception& ex) {
      DLOG_F(WARNING, "TestImportJob caught exception: {}", ex.what());
      co_return make_cancelled_report();
    } catch (...) {
      DLOG_F(WARNING, "TestImportJob caught unknown exception");
      co_return make_cancelled_report();
    }

    if (stop_token.stop_requested()) {
      co_return make_cancelled_report();
    }

    if (config_.report_progress) {
      const auto progress
        = static_cast<float>(step + 1) / static_cast<float>(step_count);
      ReportProgress(ImportPhase::kParsing, progress, progress,
        static_cast<uint32_t>(step + 1), static_cast<uint32_t>(step_count),
        "Test job running");
    }
  }

  ImportReport report {
    .cooked_root
    = Request().cooked_root.value_or(Request().source_path.parent_path()),
    .success = true,
  };

  co_return report;
}

} // namespace oxygen::content::import::test
