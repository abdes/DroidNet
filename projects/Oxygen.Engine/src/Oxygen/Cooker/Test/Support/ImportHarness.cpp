//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/ImportHarness.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::cooker::test {

namespace {

  struct SubmitState final {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    content::import::ImportReport report {};
  };

} // namespace

auto SubmitAndWait(content::import::AsyncImportService& service,
  content::import::ImportRequest request, const std::chrono::seconds timeout)
  -> std::optional<content::import::ImportReport>
{
  auto state = std::make_shared<SubmitState>();
  const auto submitted = service.SubmitImport(
    std::move(request),
    [state](const content::import::ImportJobId /*job_id*/,
      const content::import::ImportReport& completed) -> void {
      {
        const std::scoped_lock lock(state->mutex);
        state->report = completed;
        state->done = true;
      }
      state->cv.notify_all();
    },
    nullptr);
  if (!submitted.has_value()) {
    ADD_FAILURE() << "Import submission was rejected";
    return std::nullopt;
  }
  std::unique_lock lock(state->mutex);
  if (!state->cv.wait_for(lock, timeout, [&] -> bool { return state->done; })) {
    ADD_FAILURE() << "Import did not complete within " << timeout.count()
                  << " seconds";
    return std::nullopt;
  }
  return state->report;
}

auto ImportServiceTest::ServiceConfig() const
  -> content::import::AsyncImportService::Config
{
  return content::import::AsyncImportService::Config {
    .thread_pool_size = 2U,
  };
}

auto ImportServiceTest::Service() -> content::import::AsyncImportService&
{
  if (service_ == nullptr) {
    service_
      = std::make_unique<content::import::AsyncImportService>(ServiceConfig());
  }
  return *service_;
}

auto ImportServiceTest::CookedRoot() const -> std::filesystem::path
{
  return TempDir() / "cooked";
}

auto ImportServiceTest::Import(content::import::ImportRequest request)
  -> std::optional<content::import::ImportReport>
{
  return SubmitAndWait(Service(), std::move(request));
}

auto ImportServiceTest::LoadInspection(const std::filesystem::path& cooked_root)
  -> content::lc::Inspection
{
  content::lc::Inspection inspection;
  inspection.LoadFromRoot(cooked_root);
  return inspection;
}

void ImportServiceTest::TearDown()
{
  if (service_ != nullptr) {
    service_->Stop();
  }
  TempDirTest::TearDown();
}

} // namespace oxygen::cooker::test
