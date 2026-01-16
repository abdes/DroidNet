//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Tools/ImportTool/ImportRunner.h>

#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>

#include <Oxygen/Content/Import/Async/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>

namespace oxygen::content::import::tool {

namespace {

  auto PrintTelemetry(const ImportReport& report) -> void
  {
    const auto print_duration
      = [](const char* label,
          const std::optional<std::chrono::microseconds> duration) {
          if (!duration.has_value()) {
            return;
          }

          const auto ms = std::chrono::duration<double, std::milli>(*duration);
          const auto flags = std::cout.flags();
          const auto precision = std::cout.precision();
          std::cout << label << ": " << std::fixed << std::setprecision(3)
                    << ms.count() << " ms\n";
          std::cout.flags(flags);
          std::cout.precision(precision);
        };

    const auto& telemetry = report.telemetry;
    print_duration("telemetry.io", telemetry.io_duration);
    print_duration("telemetry.decode", telemetry.decode_duration);
    print_duration("telemetry.load", telemetry.load_duration);
    print_duration("telemetry.cook", telemetry.cook_duration);
    print_duration("telemetry.emit", telemetry.emit_duration);
    print_duration("telemetry.finalize", telemetry.finalize_duration);
    print_duration("telemetry.total", telemetry.total_duration);
  }

} // namespace

auto RunImportJob(const ImportRequest& request, const bool verbose,
  const bool print_telemetry) -> int
{
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<ImportReport> report;

  ImportReport report_copy {};
  std::optional<std::string> submit_error;
  bool have_report = false;
  {
    AsyncImportService service;

    const auto on_complete = [&](ImportJobId, const ImportReport& result) {
      {
        std::lock_guard lock(mutex);
        report = result;
      }
      cv.notify_one();
    };

    const auto on_progress = [&](const ImportProgress& progress) {
      if (!verbose) {
        return;
      }
      std::cout << "phase=" << static_cast<uint32_t>(progress.phase)
                << " overall=" << progress.overall_progress << "\n";
    };

    const auto job_id = service.SubmitImport(request, on_complete, on_progress);
    if (job_id == kInvalidJobId) {
      submit_error = "ERROR: failed to submit import job";
    } else {
      std::unique_lock lock(mutex);
      cv.wait(lock, [&] { return report.has_value(); });

      report_copy = *report;
      have_report = true;
    }
  }

  if (submit_error.has_value()) {
    std::cerr << *submit_error << "\n";
    return 2;
  }

  if (!have_report) {
    std::cerr << "ERROR: import failed with no report\n";
    return 2;
  }

  if (print_telemetry) {
    PrintTelemetry(report_copy);
  }

  if (!report_copy.success) {
    std::cerr << "ERROR: import failed\n";
    for (const auto& diag : report_copy.diagnostics) {
      std::cerr << "- " << diag.code << ": " << diag.message << "\n";
    }
    return 2;
  }

  std::cout << "OK: import complete\n";
  return 0;
}

} // namespace oxygen::content::import::tool
