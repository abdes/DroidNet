//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

//! Writes the engine content descriptors and the manifests that cook them,
//! without starting import workers or modifying cooked content.
class EngineContentCommand final : public ImportCommand {
public:
  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Execute() -> std::expected<void, std::error_code> override;
  [[nodiscard]] auto RequiresImportService() const noexcept -> bool override
  {
    return false;
  }

private:
  std::string sources_path_;
  std::string cooked_root_;
};

} // namespace oxygen::content::import::tool
