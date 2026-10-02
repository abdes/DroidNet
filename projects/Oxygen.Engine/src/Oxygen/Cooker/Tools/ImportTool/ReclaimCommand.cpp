//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/base.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReclaimCommand.h>

namespace oxygen::content::import::tool {

auto ReclaimCommand::Name() const -> std::string_view { return "reclaim"; }

auto ReclaimCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto record
    = clap::Option::WithKey("record")
        .Long("record")
        .About("Retained import record whose unused generations are reclaimed")
        .Required()
        .WithValue<std::string>()
        .StoreTo(&record_path_)
        .Build();
  return clap::CommandBuilder("reclaim")
    .About("Remove unselected, unleased retained generations without importing")
    .WithOption(std::move(record));
}

auto ReclaimCommand::Run() -> std::expected<void, std::error_code>
{
  const auto removed = RetainedModelImport::ReclaimUnusedGenerations(
    base::ToNativePath(record_path_));
  fmt::print("Reclaimed {} unused generation(s): {}\n", removed, record_path_);
  return {};
}

} // namespace oxygen::content::import::tool
