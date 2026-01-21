//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/Object.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::co {
class Event;
class ThreadPool;
} // namespace oxygen::co

namespace oxygen::content::import {
class IAsyncFileReader;
class IAsyncFileWriter;
class ResourceTableRegistry;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Base class for one import job executing on the import thread.
/*!
 Owns job-scoped state and defines the job lifetime boundary.

 The job is cancellable. Cancellation is reported via the completion callback
 only: `ImportReport.success=false` with a canceled diagnostic.

 The job is a `co::LiveObject`. It owns a per-job nursery which is opened by
 `ActivateAsync()` and canceled by `Stop()`. All job-scoped tasks (pipeline
 workers, collectors, and orchestration coroutines) must run in this nursery.
*/
class ImportJob : public Object, public Named, public co::LiveObject {
public:
  //! Construct a job.
  /*!
   @param job_id Unique job identifier.
   @param request Job request payload.
   @param on_complete Completion callback.
   @param on_progress Progress callback (optional).
   @param cancel_event Cancellation event shared with the service.
   @param file_reader Async file reader used by ImportSession.
   @param file_writer Async file writer used by ImportSession.
   @param thread_pool Thread pool used by ImportSession.
  */
  OXGN_CNTT_API ImportJob(ImportJobId job_id, ImportRequest request,
    ImportCompletionCallback on_complete, ImportProgressCallback on_progress,
    std::shared_ptr<co::Event> cancel_event,
    observer_ptr<IAsyncFileReader> file_reader,
    observer_ptr<IAsyncFileWriter> file_writer,
    observer_ptr<co::ThreadPool> thread_pool,
    observer_ptr<ResourceTableRegistry> table_registry,
    ImportConcurrency concurrency);

  OXYGEN_MAKE_NON_COPYABLE(ImportJob)
  OXYGEN_MAKE_NON_MOVABLE(ImportJob)

  //! Open the job nursery.
  OXGN_CNTT_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;

  //! Start job execution.
  OXGN_CNTT_API void Run() override;

  //! Request job cancellation.
  OXGN_CNTT_API void Stop() override;

  //! Returns true while the job nursery is open.
  OXGN_CNTT_NDAPI auto IsRunning() const -> bool override;

  //! Wait until the job reports completion.
  OXGN_CNTT_NDAPI auto Wait() -> co::Co<>;

  //! Get the job identifier.
  OXGN_CNTT_NDAPI auto GetJobId() const noexcept -> ImportJobId;

  //! Get the job display name.
  OXGN_CNTT_NDAPI auto GetName() const noexcept -> std::string_view override;

  //! Set the job display name.
  OXGN_CNTT_API void SetName(std::string_view name) noexcept override;

protected:
  //! Execute the job-specific import work.
  /*!
   Concrete jobs must implement this method and return a complete report.

   Cancellation is handled by the base class and is always reported via the
   completion callback.
  */
  [[nodiscard]] virtual auto ExecuteAsync() -> co::Co<ImportReport> = 0;

  //! Access the job request.
  OXGN_CNTT_NDAPI auto Request() -> ImportRequest&;
  OXGN_CNTT_NDAPI auto Request() const -> const ImportRequest&;

  //! Ensure the request has a concrete cooked root on disk.
  OXGN_CNTT_API auto EnsureCookedRoot() -> void;

  //! Access the async file writer.
  OXGN_CNTT_NDAPI auto FileReader() const noexcept
    -> observer_ptr<IAsyncFileReader>;

  //! Access the async file writer.
  OXGN_CNTT_NDAPI auto FileWriter() const noexcept
    -> observer_ptr<IAsyncFileWriter>;

  //! Access the shared thread pool.
  OXGN_CNTT_NDAPI auto ThreadPool() const noexcept
    -> observer_ptr<co::ThreadPool>;

  //! Access pipeline concurrency settings.
  OXGN_CNTT_NDAPI auto Concurrency() const noexcept -> const ImportConcurrency&;

  //! Access the resource table registry.
  OXGN_CNTT_NDAPI auto TableRegistry() const noexcept
    -> observer_ptr<ResourceTableRegistry>;

  //! Returns the job id.
  OXGN_CNTT_NDAPI auto JobId() const -> ImportJobId;

  //! Job-scoped cancellation token for pipeline work.
  /*!
   Concrete jobs should pass this token into pipeline work items so that
   compute-only pipelines can cooperatively stop expensive work.
  */
  OXGN_CNTT_NDAPI auto StopToken() const noexcept -> std::stop_token;

  //! Get the naming service for this import job.
  OXGN_CNTT_API auto GetNamingService() -> NamingService&;

  //! Start a job-scoped task in the job nursery.
  /*!
   @tparam TaskFactory Callable returning `co::Co<>`.
   @param task_factory Callable that creates the coroutine.
  */
  template <typename TaskFactory>
  auto StartTask(TaskFactory&& task_factory) -> void
  {
    DCHECK_F(nursery_ != nullptr, "ImportJob nursery is not open");
    nursery_->Start(std::forward<TaskFactory>(task_factory));
  }

  //! Start pipeline workers in the job nursery.
  /*!
   @tparam Pipeline Pipeline type supporting `Start(co::Nursery&)`.
   @param pipeline Pipeline instance.
  */
  template <typename Pipeline> auto StartPipeline(Pipeline& pipeline) -> void
  {
    DCHECK_F(nursery_ != nullptr, "ImportJob nursery is not open");
    pipeline.Start(*nursery_);
  }

  //! Access the progress callback (may be empty).
  OXGN_CNTT_NDAPI auto ProgressCallback() const noexcept
    -> const ImportProgressCallback&;

  OXGN_CNTT_API auto ReportProgress(ImportPhase phase, float overall_progress,
    float phase_progress, uint32_t items_completed, uint32_t items_total,
    std::string message) -> void;

private:
  [[nodiscard]] auto MainAsync() -> co::Co<>;

  [[nodiscard]] auto MakeCancelledReport(const ImportRequest& request) const
    -> ImportReport;

  [[nodiscard]] auto MakeNoFileWriterReport(const ImportRequest& request) const
    -> ImportReport;

  ImportJobId job_id_ = kInvalidJobId;
  ImportRequest request_;
  ImportCompletionCallback on_complete_;
  ImportProgressCallback on_progress_;
  std::shared_ptr<co::Event> cancel_event_;
  observer_ptr<IAsyncFileReader> file_reader_ {};
  observer_ptr<IAsyncFileWriter> file_writer_ {};
  observer_ptr<co::ThreadPool> thread_pool_ {};
  observer_ptr<ResourceTableRegistry> table_registry_ {};
  ImportConcurrency concurrency_ {};

  std::string name_;

  std::stop_source stop_source_;

  std::unique_ptr<NamingService> naming_service_;

  co::Nursery* nursery_ = nullptr;
  co::Event completed_;
  bool started_ = false;
};

} // namespace oxygen::content::import::detail
