//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_PAKTOOL_VERSION
#  error OXYGEN_PAKTOOL_VERSION must be defined for PakTool CLI version.
#endif

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>

#include <Oxygen/Cooker/Tools/PakTool/CliBuilder.h>

namespace oxygen::content::pak::tool {

namespace {

  using oxygen::clap::CliBuilder;
  using oxygen::clap::Command;
  using oxygen::clap::CommandBuilder;
  using oxygen::clap::Option;
  using oxygen::clap::Options;

  constexpr auto kProgramName = std::string_view { "Oxygen.Cooker.PakTool" };
  constexpr auto kVersion = std::string_view { OXYGEN_PAKTOOL_VERSION };

  auto BuildToolOptions(PakToolOutputOptions& options)
    -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Tool Options");

    group->Add(Option::WithKey("quiet")
        .About("Suppress non-error output")
        .Short("q")
        .Long("quiet")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.quiet)
        .Build());

    group->Add(Option::WithKey("no-color")
        .About("Disable ANSI color codes")
        .Long("no-color")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.no_color)
        .Build());

    group->Add(Option::WithKey("diagnostics-file")
        .About("Write structured build diagnostics to file")
        .Long("diagnostics-file")
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .StoreTo(&options.diagnostics_file)
        .Build());

    return group;
  }

  auto BuildRequestOptions(PakToolRequestOptions& options)
    -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Request Options");

    group->Add(Option::WithKey("loose-source")
        .About("Add a loose cooked source directory")
        .Long("loose-source")
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("DIR")
        .Repeatable()
        .CallOnEachValue([&options](const std::filesystem::path& path) {
          options.sources.push_back(data::CookedSource {
            .kind = data::CookedSourceKind::kLooseCooked,
            .path = path,
          });
        })
        .Build());

    group->Add(Option::WithKey("pak-source")
        .About("Add an existing pak source archive")
        .Long("pak-source")
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .Repeatable()
        .CallOnEachValue([&options](const std::filesystem::path& path) {
          options.sources.push_back(data::CookedSource {
            .kind = data::CookedSourceKind::kPak,
            .path = path,
          });
        })
        .Build());

    group->Add(Option::WithKey("out")
        .About("Final published pak output path")
        .Long("out")
        .Required()
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .StoreTo(&options.output_pak)
        .Build());

    group->Add(Option::WithKey("catalog-out")
        .About("Final published pak catalog sidecar path")
        .Long("catalog-out")
        .Required()
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .StoreTo(&options.catalog_output)
        .Build());

    group->Add(Option::WithKey("content-version")
        .About("Content version to stamp into the pak")
        .Long("content-version")
        .Required()
        .WithValue<uint16_t>()
        .UserFriendlyName("U16")
        .StoreTo(&options.content_version)
        .Build());

    group->Add(Option::WithKey("source-key")
        .About("Canonical lowercase UUIDv7 source identity")
        .Long("source-key")
        .Required()
        .WithValue<std::string>()
        .UserFriendlyName("UUIDV7")
        .StoreTo(&options.source_key)
        .Build());

    group->Add(Option::WithKey("non-deterministic")
        .About("Disable deterministic pak build behavior")
        .Long("non-deterministic")
        .WithValue<bool>()
        .DefaultValue(true, "true")
        .ImplicitValue(false, "false")
        .StoreTo(&options.deterministic)
        .Build());

    group->Add(Option::WithKey("embed-browse-index")
        .About("Embed browse index data in the pak")
        .Long("embed-browse-index")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.embed_browse_index)
        .Build());

    group->Add(Option::WithKey("no-crc32")
        .About("Disable pak CRC32 computation")
        .Long("no-crc32")
        .WithValue<bool>()
        .DefaultValue(true, "true")
        .ImplicitValue(false, "false")
        .StoreTo(&options.compute_crc32)
        .Build());

    group->Add(Option::WithKey("fail-on-warnings")
        .About("Escalate warnings into build failure")
        .Long("fail-on-warnings")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.fail_on_warnings)
        .Build());

    return group;
  }

  auto BuildFullBuildOptions(PakToolBuildCommandOptions& options)
    -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Build Options");

    group->Add(Option::WithKey("manifest-out")
        .About("Optional manifest output path for full builds")
        .Long("manifest-out")
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .StoreTo(&options.manifest_output)
        .Build());

    return group;
  }

  auto BuildPatchOptions(PakToolPatchCommandOptions& options)
    -> std::shared_ptr<Options>
  {
    auto group = std::make_shared<Options>("Patch Options");

    group->Add(Option::WithKey("base-catalog")
        .About("Base pak catalog input for patch planning")
        .Long("base-catalog")
        .Required()
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .Repeatable()
        .CallOnEachValue([&options](const std::filesystem::path& path) {
          options.base_catalogs.push_back(path);
        })
        .Build());

    group->Add(Option::WithKey("manifest-out")
        .About("Required manifest output path for patch builds")
        .Long("manifest-out")
        .Required()
        .WithValue<std::filesystem::path>()
        .UserFriendlyName("PATH")
        .StoreTo(&options.manifest_output)
        .Build());

    group->Add(Option::WithKey("allow-base-set-mismatch")
        .About("Allow base set mismatch during patch compatibility checks")
        .Long("allow-base-set-mismatch")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.allow_base_set_mismatch)
        .Build());

    group->Add(Option::WithKey("allow-content-version-mismatch")
        .About("Allow base content version mismatch during patch checks")
        .Long("allow-content-version-mismatch")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.allow_content_version_mismatch)
        .Build());

    group->Add(Option::WithKey("allow-base-source-key-mismatch")
        .About("Allow base source key mismatch during patch checks")
        .Long("allow-base-source-key-mismatch")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.allow_base_source_key_mismatch)
        .Build());

    group->Add(Option::WithKey("allow-catalog-digest-mismatch")
        .About("Allow catalog digest mismatch during patch checks")
        .Long("allow-catalog-digest-mismatch")
        .WithValue<bool>()
        .DefaultValue(false, "false")
        .ImplicitValue(true, "true")
        .StoreTo(&options.allow_catalog_digest_mismatch)
        .Build());

    return group;
  }

  auto BuildBuildCommand(PakToolCliOptions& options) -> std::shared_ptr<Command>
  {
    return CommandBuilder("build")
      .About("Build a full pak archive from cooked sources.")
      .WithOptions(BuildToolOptions(options.output))
      .WithOptions(BuildRequestOptions(options.request))
      .WithOptions(BuildFullBuildOptions(options.build))
      .Build();
  }

  auto BuildPatchCommand(PakToolCliOptions& options) -> std::shared_ptr<Command>
  {
    return CommandBuilder("patch")
      .About("Build a patch pak archive against base catalogs.")
      .WithOptions(BuildToolOptions(options.output))
      .WithOptions(BuildRequestOptions(options.request))
      .WithOptions(BuildPatchOptions(options.patch))
      .Build();
  }

} // namespace

auto BuildCli(PakToolCliOptions& options) -> std::unique_ptr<oxygen::clap::Cli>
{
  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About("Build and publish Oxygen pak archives and their sidecar artifacts.")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(BuildBuildCommand(options))
    .WithCommand(BuildPatchCommand(options))
    .Build();
}

} // namespace oxygen::content::pak::tool
