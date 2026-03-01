//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Tools/ImportTool/GlobalOptions.h>
#include <Oxygen/Cooker/Tools/ImportTool/ImportCommand.h>

namespace oxygen::content::import::tool {

class InputCommand final : public ImportCommand {
public:
  explicit InputCommand(const GlobalOptions* global_options)
    : global_options_ { global_options }
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run() -> std::expected<void, std::error_code> override;

private:
  const GlobalOptions* global_options_ = nullptr;

public:
  struct Options final {
    std::string source_path;
    std::string cooked_root;
    bool with_content_hashing = true;
    std::string job_name;
    std::string report_path;
  } options_ {};
};

} // namespace oxygen::content::import::tool
