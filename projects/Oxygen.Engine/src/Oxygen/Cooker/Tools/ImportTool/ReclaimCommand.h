//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceMountOptions.h>

namespace oxygen::content::import::tool {

//! Reclaim retired generations through the model import record publication.
class ReclaimCommand final : public ImportCommand {
public:
  explicit ReclaimCommand(const GlobalOptions* global_options)
    : global_options_ { global_options }
  {
  }
  ~ReclaimCommand() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ReclaimCommand)
  OXYGEN_MAKE_NON_MOVABLE(ReclaimCommand)

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Execute() -> std::expected<void, std::error_code> override;
  [[nodiscard]] auto RequiresImportService() const noexcept -> bool override
  {
    return false;
  }

private:
  const GlobalOptions* global_options_ = nullptr;
  //! The mounts the project's records are found through.
  SourceMountOptions mount_options_ {};
  std::string record_path_;
  std::string generations_root_;
};

} // namespace oxygen::content::import::tool
