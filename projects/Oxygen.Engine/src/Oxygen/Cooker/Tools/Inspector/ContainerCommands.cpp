//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/ContainerCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DumpFormat.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

namespace oxygen::content::inspection {

namespace {

  using oxygen::data::loose_cooked::FileKind;

  auto AssetTypeToString(const uint8_t asset_type) -> std::string_view
  {
    using oxygen::data::AssetType;

    const auto max = static_cast<uint8_t>(AssetType::kMaxAssetType);
    if (asset_type > max) {
      return "unknown";
    }

    return nostd::to_string(static_cast<AssetType>(asset_type));
  }

  // TODO: replace with nostd::to_string defined with the enum itself
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
    case FileKind::kScriptsTable:
      return "scripts.table";
    case FileKind::kScriptsData:
      return "scripts.data";
    case FileKind::kPhysicsTable:
      return "physics.table";
    case FileKind::kPhysicsData:
      return "physics.data";
    case FileKind::kAuxiliary:
      return "auxiliary";
    case FileKind::kTextureDescriptor:
      return "texture";
    case FileKind::kUnknown:
    default:
      return "unknown";
    }
  }

  constexpr int kFileKindColumn = 15;
  constexpr int kFilePathColumn = 34;

  auto DumpHexSha256(std::ostream& os,
    const std::span<const uint8_t, oxygen::data::loose_cooked::kSha256Size>&
      digest) -> void
  {
    for (const auto b : digest) {
      os << HexHigh(b) << HexLow(b);
    }
  }

  auto DumpFileRecords(
    const std::span<const oxygen::content::lc::Inspection::FileEntry> entries,
    std::ostream& os) -> void
  {
    if (entries.empty()) {
      os << "(none)\n";
      return;
    }

    os << "Kind             Path                                Size        "
          "Virtual Path\n";
    os << "---------------  ----------------------------------  ----------  "
          "------------\n";

    for (const auto& e : entries) {
      os << std::left << std::setw(kFileKindColumn) << FileKindToString(e.kind)
         << "  ";
      os << std::left << std::setw(kFilePathColumn) << e.relpath << "  ";
      os << std::right << std::setw(kSizeColumn) << e.size;
      if (!e.virtual_path.empty()) {
        os << "  " << e.virtual_path;
      }
      os << "\n";
    }
  }

  auto DumpAssets(
    const std::span<const oxygen::content::lc::Inspection::AssetEntry> entries,
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
            std::span<const uint8_t, oxygen::data::loose_cooked::kSha256Size>(
              e.descriptor_sha256->data(), e.descriptor_sha256->size()));
        }
      }

      os << "\n";
    }
  }

  auto RunValidate(std::ostream& out, const ValidateOptions& opts)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(opts.cooked_root);

    if (std::filesystem::is_regular_file(cooked_root)) {
      const oxygen::content::PakFile pak(cooked_root);
      pak.ValidateCrc32Integrity();
      if (!opts.quiet) {
        out << "OK: valid pak: " << cooked_root.string() << "\n";
      }
      return {};
    }
    oxygen::content::lc::ValidateRoot(cooked_root);
    if (!opts.quiet) {
      out << "OK: valid loose cooked root: " << cooked_root.string() << "\n";
    }
    return {};
  }

  auto RunDumpIndex(std::ostream& out, const DumpOptions& opts)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(opts.cooked_root);

    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const bool dump_assets = opts.assets || (!opts.assets && !opts.files);
    const bool dump_files = opts.files || (!opts.assets && !opts.files);

    out << "Cooked Root : " << cooked_root.string() << "\n";
    out << "Index GUID  : " << nostd::to_string(inspection.Guid()) << "\n";

    if (dump_assets) {
      out << "\nAssets (" << inspection.Assets().size() << ")\n";
      DumpAssets(inspection.Assets(), out, opts.show_digests);
    }

    if (dump_files) {
      out << "\nFile Records (" << inspection.Files().size() << ")\n";
      DumpFileRecords(inspection.Files(), out);
    }

    return {};
  }

} // namespace

auto ValidateCommand::Name() const -> std::string_view { return "validate"; }

auto ValidateCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  return clap::CommandBuilder("validate")
    .About("Validate a loose cooked root (index and files) or a pak "
           "(structure, catalog and CRC).")
    .WithPositionalArguments(CookedRootArgument(
      &options_.cooked_root, "Loose cooked root directory or pak file"))
    .WithOption(clap::Option::WithKey("quiet")
        .About("Do not print on success")
        .Short("q")
        .Long("quiet")
        .WithValue<bool>()
        .StoreTo(&options_.quiet)
        .Build());
}

auto ValidateCommand::Run(toolkit::ToolContext& context) -> toolkit::ToolResult
{
  return RunValidate(context.Out(), options_);
}

auto IndexCommand::Name() const -> std::string_view { return "index"; }

auto IndexCommand::BuildCommand() -> std::shared_ptr<clap::Command>
{
  return clap::CommandBuilder("index")
    .About("Dump container.index.bin contents (validated).")
    .WithPositionalArguments(CookedRootArgument(&options_.cooked_root))
    .WithOption(clap::Option::WithKey("assets")
        .About("Dump asset entries")
        .Long("assets")
        .WithValue<bool>()
        .StoreTo(&options_.assets)
        .Build())
    .WithOption(clap::Option::WithKey("files")
        .About("Dump file records")
        .Long("files")
        .WithValue<bool>()
        .StoreTo(&options_.files)
        .Build())
    .WithOption(clap::Option::WithKey("digests")
        .About("Include SHA-256 digests")
        .Long("digests")
        .WithValue<bool>()
        .StoreTo(&options_.show_digests)
        .Build());
}

auto IndexCommand::Run(toolkit::ToolContext& context) -> toolkit::ToolResult
{
  return RunDumpIndex(context.Out(), options_);
}

} // namespace oxygen::content::inspection
