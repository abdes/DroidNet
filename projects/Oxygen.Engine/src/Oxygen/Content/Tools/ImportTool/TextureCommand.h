//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Content/Import/TextureImportSettings.h>
#include <Oxygen/Content/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Content/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

class TextureCommand final : public ImportCommand {
public:
  explicit TextureCommand(const GlobalOptions* global_options)
    : global_options_ { global_options }
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> int override;

private:
  const GlobalOptions* global_options_ = nullptr;

public:
  TextureImportSettings options_ {};
};

} // namespace oxygen::content::import::tool
