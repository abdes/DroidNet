//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>

namespace oxygen::content::inspection {

auto CookedRootArgument(std::string* target, const std::string_view about)
  -> std::shared_ptr<clap::Option>
{
  return clap::Option::Positional("cooked_root")
    .About(std::string(about))
    .Required()
    .WithValue<std::string>()
    .StoreTo(target)
    .Build();
}

auto CookedRootCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  return clap::CommandBuilder(std::string(spec_.name))
    .About(std::string(spec_.about))
    .WithPositionalArguments(CookedRootArgument(&cooked_root_));
}

auto CookedRootCommand::Run(toolkit::ToolContext& context)
  -> toolkit::ToolResult
{
  return spec_.dump(context.Out(), cooked_root_);
}

} // namespace oxygen::content::inspection
