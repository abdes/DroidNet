//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/EngineContent.h>
#include <Oxygen/Cooker/Tools/ImportTool/EngineContentCommand.h>

namespace oxygen::content::import::tool {

auto EngineContentCommand::Name() const -> std::string_view
{
  return "engine-content";
}

auto EngineContentCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto sources = clap::Option::Positional("sources")
                   .About("Directory that receives the descriptors and the "
                          "materials and geometry import manifests")
                   .Required()
                   .WithValue<std::string>()
                   .StoreTo(&sources_path_)
                   .Build();
  auto cooked_root = clap::Option::Positional("cooked-root")
                       .About("Loose cooked root the manifests cook into")
                       .Required()
                       .WithValue<std::string>()
                       .StoreTo(&cooked_root_)
                       .Build();
  return clap::CommandBuilder("engine-content")
    .About("Write the engine content sources under their /Engine paths")
    .WithPositionalArguments(sources, cooked_root);
}

auto EngineContentCommand::Run() -> std::expected<void, std::error_code>
{
  [[maybe_unused]] const auto manifests
    = WriteEngineContentSources(std::filesystem::absolute(sources_path_),
      std::filesystem::absolute(cooked_root_));
  return {};
}

} // namespace oxygen::content::import::tool
