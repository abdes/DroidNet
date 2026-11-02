//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>

#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Tools/Common/EnumString.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/ContainerCommands.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DumpFormat.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedSource.h>

namespace oxygen::content::inspection {

namespace {

  auto TableKindToString(const cooked::TableKind kind) -> std::string_view
  {
    switch (kind) {
    case cooked::TableKind::kBuffer:
      return "buffers";
    case cooked::TableKind::kTexture:
      return "textures";
    case cooked::TableKind::kScript:
      return "scripts";
    case cooked::TableKind::kPhysics:
      return "physics";
    case cooked::TableKind::kAudio:
      return "audio";
    }
    return "unknown";
  }

  constexpr int kFileKindColumn = 20;
  constexpr int kFilePathColumn = 34;

  constexpr auto kTableKinds = std::array {
    cooked::TableKind::kBuffer,
    cooked::TableKind::kTexture,
    cooked::TableKind::kScript,
    cooked::TableKind::kPhysics,
    cooked::TableKind::kAudio,
  };

  auto DumpHexSha256(std::ostream& os, const base::Sha256Digest& digest) -> void
  {
    os << base::ToHexString(std::as_bytes(std::span(digest)));
  }

  //! The resource tables, named textures and sidecars of `content`.
  auto DumpStorage(const cooked::CookedContent& content, std::ostream& os)
    -> void
  {
    os << "Kind                  Rows        Stored File\n";
    os << "--------------------  ----------  "
          "----------------------------------\n";
    for (const auto kind : kTableKinds) {
      const auto& table = content.ResourceTable(kind);
      os << std::left << std::setw(kFileKindColumn) << TableKindToString(kind)
         << "  " << std::right << std::setw(kSizeColumn) << table.RowCount()
         << "  ";
      if (table.RowCount() > 0U) {
        os << table.RowsStored().file.generic_string();
      }
      os << "\n";
    }

    os << "\nNamed Textures(" << content.NamedTextures().size() << ") ";
    if (content.NamedTextures().empty()) {
      os << "(none)\n";
    }
    for (const auto& texture : content.NamedTextures()) {
      os << "- vpath='" << texture.virtual_path << "' row=" << texture.row.get()
         << "\n";
    }

    os << "\nSidecars(" << content.Sidecars().size() << ") ";
    if (content.Sidecars().empty()) {
      os << "(none)\n";
    }
    for (const auto& sidecar : content.Sidecars()) {
      os << std::left << std::setw(kFilePathColumn) << sidecar.name << "  "
         << std::right << std::setw(kSizeColumn)
         << sidecar.stored.range.count.get();
      if (!sidecar.source_path.empty()) {
        os << "  " << sidecar.source_path;
      }
      os << "\n";
    }
  }

  auto DumpAssets(const std::span<const cooked::CookedAsset> entries,
    std::ostream& os, const bool show_digests) -> void
  {
    if (entries.empty()) {
      os << "(none)\n";
      return;
    }

    for (const auto& e : entries) {
      os << "- key='" << oxygen::data::to_string(e.key) << "'";
      os << " type=" << toolkit::EnumString(e.type);

      if (!e.virtual_path.empty()) {
        os << " vpath='" << e.virtual_path << "'";
      }

      os << " desc_size=" << e.descriptor_size.get();

      if (show_digests) {
        os << " desc_sha256=";
        DumpHexSha256(os, e.descriptor_sha256);
      }

      os << "\n";
    }
  }

  auto RunValidate(std::ostream& out, const ValidateOptions& opts)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(opts.cooked_root);

    const auto content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);
    cooked::Validate(content);
    if (!opts.quiet) {
      out << "OK: valid "
          << (content.Source().kind == data::CookedSourceKind::kPak
                 ? "pak"
                 : "loose cooked root")
          << ": " << cooked_root.string() << "\n";
    }
    return {};
  }

  auto RunDumpIndex(std::ostream& out, const DumpOptions& opts)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(opts.cooked_root);

    const auto content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);

    const bool dump_assets = opts.assets || (!opts.assets && !opts.files);
    const bool dump_files = opts.files || (!opts.assets && !opts.files);

    out << "Cooked Root : " << cooked_root.string() << "\n";
    out << "Source Key  : " << nostd::to_string(content.Summary().source_key)
        << "\n";

    if (dump_assets) {
      out << "\nAssets (" << content.Assets().size() << ")\n";
      DumpAssets(content.Assets(), out, opts.show_digests);
    }

    if (dump_files) {
      out << "\nStorage\n";
      DumpStorage(content, out);
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
