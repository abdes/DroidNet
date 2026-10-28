//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

//! A report module as a tool command.
/*!
 Each report (asset keys, dependencies, geometry, scenes, scripts, inventory)
 exposes an `Options` struct, a `Build*Command` that parses into it and a `Run*`
 that writes its report file. This binds the three to a command name.
*/
template <typename Options>
class ReportCommand final : public toolkit::ToolCommand {
public:
  struct Spec {
    std::string_view name;
    auto (*build)(Options&) -> std::shared_ptr<clap::Command>;
    auto (*run)(const Options&) -> toolkit::ToolResult;
  };

  explicit ReportCommand(const Spec& spec)
    : spec_(spec)
  {
  }

  [[nodiscard]] auto Name() const -> std::string_view override
  {
    return spec_.name;
  }

  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override
  {
    return spec_.build(options_);
  }

  [[nodiscard]] auto Run(toolkit::ToolContext& /*context*/)
    -> toolkit::ToolResult override
  {
    return spec_.run(options_);
  }

private:
  Spec spec_;
  Options options_ {};
};

} // namespace oxygen::content::inspection
