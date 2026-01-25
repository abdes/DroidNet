//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportManifest.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

class BatchCommand final : public ImportCommand {
public:
  explicit BatchCommand(const GlobalOptions* global_options)
    : global_options_ { global_options }
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> std::expected<void, std::error_code> override;
  //! Prepare import service configuration from the batch manifest.
  [[nodiscard]] auto PrepareImportServiceConfig()
    -> std::expected<AsyncImportService::Config, std::error_code> override;

  //! Provide service configuration overrides for batch execution.
  auto SetServiceConfigOverrides(const AsyncImportService::Config* config,
    bool concurrency_override_set) -> void
  {
    service_config_override_ = config;
    concurrency_override_set_ = concurrency_override_set;
  }

private:
  const GlobalOptions* global_options_ = nullptr;
  std::optional<ImportManifest> prepared_manifest_;
  const AsyncImportService::Config* service_config_override_ = nullptr;
  bool concurrency_override_set_ = false;

  struct Options {
    std::string manifest_path;
    std::string root_path;
    std::string report_path;
    uint32_t max_in_flight_jobs = 0U;
    bool max_in_flight_jobs_set = false;
    bool dry_run = false;
    bool fail_fast = false;
    bool quiet = false;
  } options_ {};
};

} // namespace oxygen::content::import::tool
