//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <string_view>
#include <system_error>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Content/Import/AsyncImportService.h>

namespace oxygen::content::import::tool {

class ImportCommand {
public:
  virtual ~ImportCommand() = default;

  [[nodiscard]] virtual auto Name() const -> std::string_view = 0;
  [[nodiscard]] virtual auto BuildCommand() -> std::shared_ptr<clap::Command>
    = 0;
  [[nodiscard]] virtual auto Run() -> std::expected<void, std::error_code> = 0;

  //! Prepare import service configuration for this command.
  [[nodiscard]] virtual auto PrepareImportServiceConfig()
    -> std::expected<AsyncImportService::Config, std::error_code>
  {
    AsyncImportService::Config config {};
    config.max_in_flight_jobs = 1U;
    return config;
  }
};

} // namespace oxygen::content::import::tool
