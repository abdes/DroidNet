//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

namespace oxygen::content::internal {

auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }

} // namespace oxygen::content::internal

namespace {

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

using oxygen::data::AssetKey;
using oxygen::data::loose_cooked::v1::FileKind;

constexpr std::string_view kProgramName = "Oxygen.Content.Inspector";
constexpr std::string_view kVersion = "0.1";

struct ValidateOptions {
  std::string cooked_root;
  bool quiet = false;
};

struct DumpOptions {
  std::string cooked_root;
  bool assets = false;
  bool files = false;
  bool show_digests = false;
};

auto AssetTypeToString(const uint8_t asset_type) -> std::string_view
{
  using oxygen::data::AssetType;

  const auto max = static_cast<uint8_t>(AssetType::kMaxAssetType);
  if (asset_type > max) {
    return "unknown";
  }

  return oxygen::data::to_string(static_cast<AssetType>(asset_type));
}

auto FileKindToString(const FileKind kind) -> std::string_view
{
  switch (kind) {
  case FileKind::kBuffersTable:
    return "buffers.table";
  case FileKind::kBuffersData:
    return "buffers.data";
  case FileKind::kTexturesTable:
    return "textures.table";
  case FileKind::kTexturesData:
    return "textures.data";
  case FileKind::kUnknown:
  default:
    return "unknown";
  }
}

auto IsAllZero(
  const std::array<uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>&
    digest) -> bool
{
  for (const auto b : digest) {
    if (b != 0) {
      return false;
    }
  }
  return true;
}

auto DumpHexSha256(std::ostream& os,
  const std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>&
    digest) -> void
{
  static constexpr char kHex[] = "0123456789abcdef";
  for (const auto b : digest) {
    os << kHex[(b >> 4) & 0x0f] << kHex[b & 0x0f];
  }
}

auto DumpFileRecords(
  const std::span<const oxygen::content::LooseCookedInspection::FileEntry>
    entries,
  std::ostream& os, const bool show_digests) -> void
{
  bool any = false;
  for (const auto& e : entries) {
    any = true;

    os << "- " << FileKindToString(e.kind) << ": path='" << e.relpath << "'";
    os << " size=" << e.size;

    if (show_digests) {
      if (e.sha256) {
        os << " sha256=";
        DumpHexSha256(os,
          std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>(
            e.sha256->data(), e.sha256->size()));
      }
    }

    os << "\n";
  }

  if (!any) {
    os << "(none)\n";
  }
}

auto DumpAssets(
  const std::span<const oxygen::content::LooseCookedInspection::AssetEntry>
    entries,
  std::ostream& os, const bool show_digests) -> void
{
  if (entries.empty()) {
    os << "(none)\n";
    return;
  }

  for (const auto& e : entries) {
    os << "- key='" << oxygen::data::to_string(e.key) << "'";
    os << " type='" << AssetTypeToString(e.asset_type) << "'(";
    os << static_cast<unsigned>(e.asset_type) << ")'";

    if (!e.virtual_path.empty()) {
      os << " vpath='" << e.virtual_path << "'";
    }

    if (!e.descriptor_relpath.empty()) {
      os << " desc='" << e.descriptor_relpath << "'";
    }

    os << " desc_size=" << e.descriptor_size;

    if (show_digests) {
      if (e.descriptor_sha256) {
        os << " desc_sha256=";
        DumpHexSha256(os,
          std::span<const uint8_t, oxygen::data::loose_cooked::v1::kSha256Size>(
            e.descriptor_sha256->data(), e.descriptor_sha256->size()));
      }
    }

    os << "\n";
  }
}

auto ValidateRootOrThrow(const std::filesystem::path& cooked_root) -> void
{
  using oxygen::content::internal::EngineTagFactory;

  oxygen::content::AssetLoader loader(EngineTagFactory::Get());
  loader.AddLooseCookedRoot(cooked_root);
}

auto RunValidate(const ValidateOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    ValidateRootOrThrow(cooked_root);
    if (!opts.quiet) {
      std::cout << "OK: valid loose cooked root: " << cooked_root.string()
                << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpIndex(const DumpOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::LooseCookedInspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const bool dump_assets = opts.assets || (!opts.assets && !opts.files);
    const bool dump_files = opts.files || (!opts.assets && !opts.files);

    std::cout << "cooked_root='" << cooked_root.string() << "'\n";
    std::cout << "index_guid='" << oxygen::data::to_string(inspection.Guid())
              << "'\n";

    if (dump_assets) {
      std::cout << "\n[assets]\n";
      DumpAssets(inspection.Assets(), std::cout, opts.show_digests);
    }

    if (dump_files) {
      std::cout << "\n[file_records]\n";
      DumpFileRecords(inspection.Files(), std::cout, opts.show_digests);
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto BuildCli(ValidateOptions& validate_opts, DumpOptions& dump_opts)
  -> std::unique_ptr<Cli>
{
  auto validate_root = Option::Positional("cooked_root")
                         .About("Loose cooked root directory")
                         .Required()
                         .WithValue<std::string>()
                         .StoreTo(&validate_opts.cooked_root)
                         .Build();

  auto validate_quiet = Option::WithKey("quiet")
                          .About("Do not print on success")
                          .Short("q")
                          .Long("quiet")
                          .WithValue<bool>()
                          .StoreTo(&validate_opts.quiet)
                          .Build();

  const std::shared_ptr<Command> validate_cmd
    = CommandBuilder("validate-root")
        .About("Validate a loose cooked root (index + files).")
        .WithPositionalArguments(validate_root)
        .WithOption(std::move(validate_quiet));

  auto dump_root = Option::Positional("cooked_root")
                     .About("Loose cooked root directory")
                     .Required()
                     .WithValue<std::string>()
                     .StoreTo(&dump_opts.cooked_root)
                     .Build();

  auto dump_assets = Option::WithKey("assets")
                       .About("Dump asset entries")
                       .Long("assets")
                       .WithValue<bool>()
                       .StoreTo(&dump_opts.assets)
                       .Build();

  auto dump_files = Option::WithKey("files")
                      .About("Dump file records")
                      .Long("files")
                      .WithValue<bool>()
                      .StoreTo(&dump_opts.files)
                      .Build();

  auto dump_digests = Option::WithKey("digests")
                        .About("Include SHA-256 digests")
                        .Long("digests")
                        .WithValue<bool>()
                        .StoreTo(&dump_opts.show_digests)
                        .Build();

  const std::shared_ptr<Command> dump_cmd
    = CommandBuilder("dump-index")
        .About("Dump container.index.bin contents (validated).")
        .WithPositionalArguments(dump_root)
        .WithOption(std::move(dump_assets))
        .WithOption(std::move(dump_files))
        .WithOption(std::move(dump_digests));

  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About(
      "Inspect and validate loose cooked content roots (filesystem-backed).")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(validate_cmd)
    .WithCommand(dump_cmd)
    .Build();
}

} // namespace

auto main(int argc, char** argv) -> int
{
  try {
    ValidateOptions validate_opts;
    DumpOptions dump_opts;

    const auto cli = BuildCli(validate_opts, dump_opts);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      return 0;
    }

    if (command_path == "validate-root") {
      return RunValidate(validate_opts);
    }

    if (command_path == "dump-index") {
      return RunDumpIndex(dump_opts);
    }

    std::cerr << "ERROR: Unknown command\n";
    return 1;
  } catch (const std::exception& /*ex*/) {
    // The error is already printed by the CLI parser.
    return 3;
  }
}
