//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

//! Exports a catalog without starting import workers or modifying cooked
//! content.
class BuiltinCatalogCommand final : public ImportCommand {
public:
  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> std::expected<void, std::error_code> override;
  [[nodiscard]] auto RequiresImportService() const noexcept -> bool override
  {
    return false;
  }

private:
  std::string output_path_;
  std::string mount_name_ = "Content";
};

} // namespace oxygen::content::import::tool
