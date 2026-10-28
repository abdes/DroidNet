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

//! Remove cooked assets and auxiliary files from a loose cooked root.
/*!
 Reads a JSON request with `virtual_paths` and `auxiliary_files` arrays and
 removes their index entries and files in one index update, without cooking.
 An optional `source_key` sets the identity of the updated index, as a cook
 into a fresh generation does.
*/
class RemoveCookedCommand final : public ImportCommand {
public:
  RemoveCookedCommand() = default;
  ~RemoveCookedCommand() override = default;
  OXYGEN_MAKE_NON_COPYABLE(RemoveCookedCommand)
  OXYGEN_MAKE_NON_MOVABLE(RemoveCookedCommand)

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Execute() -> std::expected<void, std::error_code> override;
  [[nodiscard]] auto RequiresImportService() const noexcept -> bool override
  {
    return false;
  }

private:
  std::string cooked_root_;
  std::string request_path_;
};

} // namespace oxygen::content::import::tool
