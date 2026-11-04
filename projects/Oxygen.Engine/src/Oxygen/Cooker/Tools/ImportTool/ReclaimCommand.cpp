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
#include <vector>

#include <fmt/base.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Tools/ImportTool/ReclaimCommand.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceMountOptions.h>

namespace oxygen::content::import::tool {

auto ReclaimCommand::Name() const -> std::string_view { return "reclaim"; }

auto ReclaimCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  auto record
    = clap::Option::WithKey("record")
        .Long("record")
        .About("Model import record whose unused generations are reclaimed")
        .Required()
        .WithValue<std::string>()
        .StoreTo(&record_path_)
        .Build();
  auto generations
    = clap::Option::WithKey("output")
        .About("Generations folder the record's imports published into")
        .Short("i")
        .Long("output")
        .Required()
        .WithValue<std::string>()
        .StoreTo(&generations_root_)
        .Build();
  return clap::CommandBuilder("reclaim")
    .About(
      "Remove unselected, unleased model import generations without importing")
    .WithOption(std::move(record))
    .WithOption(std::move(generations))
    .WithOptions(BuildSourceMountOptions(mount_options_));
}

auto ReclaimCommand::Execute() -> std::expected<void, std::error_code>
{
  DCHECK_F(global_options_ != nullptr && global_options_->writer,
    "Global message writer must be set by main");
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto mounts
    = ResolveSourceMounts(mount_options_, global_options_->writer, diagnostics);
  if (!mounts.has_value()) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  const auto removed = ModelImportRecord::ReclaimUnusedGenerations(
    base::ToNativePath(record_path_),
    ModelImportGenerations { .root = base::ToNativePath(generations_root_) },
    *mounts);
  fmt::print("Reclaimed {} unused generation(s): {}\n", removed, record_path_);
  return {};
}

} // namespace oxygen::content::import::tool
