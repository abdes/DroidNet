//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

//! The positional `cooked_root` argument every inspection command takes.
[[nodiscard]] auto CookedRootArgument(
  std::string* target, std::string_view about = "Loose cooked root directory")
  -> std::shared_ptr<clap::Option>;

//! How to build a `CookedRootCommand`.
struct CookedRootCommandSpec {
  std::string_view name;
  std::string_view about;
  //! Dumps what the cooked root at the given path holds.
  auto (*dump)(std::ostream&, const std::string&) -> toolkit::ToolResult;
};

//! A command over one cooked root, `<name> <cooked_root>`, that prints a dump.
class CookedRootCommand final : public toolkit::ToolCommand {
public:
  explicit CookedRootCommand(const CookedRootCommandSpec& spec)
    : spec_(spec)
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override
  {
    return spec_.name;
  }

  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run(toolkit::ToolContext& context)
    -> toolkit::ToolResult override;

private:
  CookedRootCommandSpec spec_;
  std::string cooked_root_;
};

} // namespace oxygen::content::inspection
