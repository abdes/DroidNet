//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

//! Reclaim retired generations through the native retained publication owner.
class ReclaimCommand final : public ImportCommand {
public:
  ReclaimCommand() = default;
  ~ReclaimCommand() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ReclaimCommand)
  OXYGEN_MAKE_NON_MOVABLE(ReclaimCommand)

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> std::expected<void, std::error_code> override;
  [[nodiscard]] auto RequiresImportService() const noexcept -> bool override
  {
    return false;
  }

private:
  std::string record_path_ {};
};

} // namespace oxygen::content::import::tool
