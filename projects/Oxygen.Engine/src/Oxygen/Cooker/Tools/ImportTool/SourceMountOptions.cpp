//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Tools/ImportTool/MessageWriter.h>
#include <Oxygen/Cooker/Tools/ImportTool/SourceMountOptions.h>

namespace oxygen::content::import::tool {

namespace {

  auto Report(const std::span<const ImportDiagnostic> diagnostics,
    const observer_ptr<IMessageWriter> writer) -> void
  {
    for (const auto& diagnostic : diagnostics) {
      const auto line
        = fmt::format("[{}] {}", diagnostic.code, diagnostic.message);
      if (diagnostic.severity == ImportSeverity::kError) {
        writer->Error(line);
      } else {
        writer->Warning(line);
      }
    }
  }

} // namespace

auto BuildSourceMountOptions(SourceMountOptions& options)
  -> std::shared_ptr<clap::Options>
{
  auto group = std::make_shared<clap::Options>("Source Mount Options");
  group->Add(clap::Option::WithKey("mount")
      .About("Declare a source mount as <Name>=<dir>; repeat for several. "
             "The positional source must lie in one")
      .Long("mount")
      .WithValue<std::string>()
      .UserFriendlyName("NAME=DIR")
      .Repeatable()
      .CallOnEachValue([&options](const std::string& mount) -> void {
        options.mounts.push_back(mount);
      })
      .Build());
  group->Add(clap::Option::WithKey("mounts")
      .About("A mounts file applied above every .mounts.json")
      .Long("mounts")
      .WithValue<std::string>()
      .StoreTo(&options.mounts_file)
      .Build());
  group->Add(clap::Option::WithKey("root")
      .About("The project root: its .mounts.json and model import records "
             "belong to it. Default: the working directory")
      .Long("root")
      .WithValue<std::string>()
      .StoreTo(&options.root)
      .Build());
  return group;
}

auto ResolveSourceMounts(const SourceMountOptions& options,
  const observer_ptr<IMessageWriter> writer) -> std::optional<MountTable>
{
  auto declarations = std::vector<MountDeclaration> {};
  for (const auto& mount : options.mounts) {
    const auto equals = mount.find('=');
    if (equals == std::string::npos || equals == 0
      || equals + 1 == mount.size()) {
      writer->Error(fmt::format("--mount '{}' is not <Name>=<dir>", mount));
      return std::nullopt;
    }
    declarations.push_back({
      .name = mount.substr(0, equals),
      .kind = MountKind::kSource,
      .path = mount.substr(equals + 1),
    });
  }
  const auto working_directory = std::filesystem::current_path();
  auto table = ResolveMountTable({
    .project_root = options.root.empty() ? working_directory
                                         : std::filesystem::path(options.root),
    .declarations = std::move(declarations),
    .form = MountDeclarationForm::kCommandLine,
    .declared_in = "--mount",
    .working_directory = working_directory,
    .mounts_option = options.mounts_file.empty()
      ? std::nullopt
      : std::optional { std::filesystem::path(options.mounts_file) },
    .owner = MountTableOwner::kProject,
  });
  Report(table.diagnostics, writer);
  if (!table.IsValid()) {
    return std::nullopt;
  }
  return table;
}

auto LocateRequestSource(ImportRequest& request, const MountTable& mounts,
  const observer_ptr<IMessageWriter> writer) -> bool
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto located = mounts.MountPathOf(
    std::filesystem::absolute(request.source_path), diagnostics);
  Report(diagnostics, writer);
  if (!located.has_value()) {
    return false;
  }
  request.source_mount_path = located->mount_path;
  return true;
}

} // namespace oxygen::content::import::tool
