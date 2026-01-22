//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <memory>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/Internal/ImportJob.h>

namespace oxygen::content::import::test {

//! Minimal import job for testing custom job submission and cancellation.
/*!
 Simulates work by sleeping in small steps on the thread pool, reporting
 progress between steps. Cancellation is honored via the base import job logic.
*/
class TestImportJob final : public detail::ImportJob {
  OXYGEN_TYPED(TestImportJob)
public:
  //! Configuration for simulated work.
  struct Config {
    //! Total simulated duration.
    std::chrono::milliseconds total_delay { 30 };

    //! Delay per step, used to allow cancellation checks.
    std::chrono::milliseconds step_delay { 5 };

    //! Emit progress updates between steps.
    bool report_progress = true;
  };

  //! Construct a test job.
  TestImportJob(ImportJobId job_id, ImportRequest request,
    ImportCompletionCallback on_complete, ProgressEventCallback on_progress,
    std::shared_ptr<co::Event> cancel_event,
    oxygen::observer_ptr<IAsyncFileReader> file_reader,
    oxygen::observer_ptr<IAsyncFileWriter> file_writer,
    oxygen::observer_ptr<co::ThreadPool> thread_pool,
    oxygen::observer_ptr<ResourceTableRegistry> table_registry,
    ImportConcurrency concurrency, Config config = {});

protected:
  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

private:
  Config config_;
};

} // namespace oxygen::content::import::test
