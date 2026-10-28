//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DumpFormat.h>
#include <Oxygen/Cooker/Tools/Inspector/ResourceTableCommands.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

namespace {

  using oxygen::data::loose_cooked::FileKind;

  constexpr int kOffsetColumn = 19;
  constexpr int kStrideColumn = 6;
  constexpr int kFormatColumn = 15;
  constexpr int kDimensionsColumn = 11;
  constexpr int kLayersColumn = 6;
  constexpr int kTextureTypeColumn = 14;
  constexpr int kScriptEncodingColumn = 25;
  constexpr int kScriptHashColumn = 66;

  auto ToHexBytes(const std::span<const uint8_t> bytes) -> std::string
  {
    auto out = std::string {};
    out.reserve(2 + (bytes.size() * 2));
    out.append("0x");
    for (const auto byte : bytes) {
      out.push_back(HexHigh(byte));
      out.push_back(HexLow(byte));
    }
    return out;
  }

  auto FindFileRelPath(const oxygen::content::lc::Inspection& inspection,
    const FileKind kind) -> std::optional<std::string>
  {
    for (const auto& entry : inspection.Files()) {
      if (entry.kind == kind) {
        return entry.relpath;
      }
    }
    return std::nullopt;
  }

  auto FindFileRelPathBySuffix(
    const oxygen::content::lc::Inspection& inspection,
    const std::string_view suffix) -> std::optional<std::string>
  {
    for (const auto& entry : inspection.Files()) {
      if (entry.relpath.ends_with(suffix)) {
        return entry.relpath;
      }
    }
    return std::nullopt;
  }

  template <typename T>
  auto LoadPackedTable(const std::filesystem::path& table_path)
    -> std::vector<T>
  {
    using oxygen::serio::FileStream;
    using oxygen::serio::Reader;

    FileStream<> stream(table_path, std::ios::in);
    const auto size_result = stream.Size();
    if (!size_result) {
      throw std::runtime_error("failed to get table size");
    }

    const auto size_bytes = size_result.value();
    if (size_bytes == 0) {
      return {};
    }

    if (size_bytes % sizeof(T) != 0) {
      throw std::runtime_error("table size is not a multiple of entry size");
    }

    const auto count = size_bytes / sizeof(T);
    std::vector<T> entries(count);

    Reader<FileStream<>> reader(stream);
    const auto pack = reader.ScopedAlignment(1);
    auto read_result = reader.ReadBlobInto(
      std::as_writable_bytes(std::span<T>(entries.data(), entries.size())));
    if (!read_result) {
      throw std::runtime_error("failed to read table file");
    }

    return entries;
  }

  auto RunDumpBuffers(std::ostream& out, const std::string& cooked_root_path)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(cooked_root_path);

    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kBuffersTable);
    if (!relpath) {
      return std::unexpected(
        toolkit::ToolError::Failed("buffers.table not found in index"));
    }

    const auto table_path = cooked_root / *relpath;
    auto entries = LoadPackedTable<oxygen::data::pak::core::BufferResourceDesc>(
      table_path);

    if (entries.empty()) {
      out << "No buffers found in: '" << table_path.string() << "'\n";
      return {};
    }

    out << "Dumping " << entries.size() - 1 << " user buffers ("
        << entries.size() << " total entries including sentinel) in: '"
        << table_path.string() << "'\n\n";

    // clang-format off
    out << "Idx  Offset              Size       Stride  Format          Usage Flags                      Hash\n";
    out << "---- ------------------- ---------- ------ --------------- --------------------------------  ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries.at(i);
      const auto* const format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.element_format));
      const auto usage_name = nostd::to_string(
        static_cast<oxygen::data::BufferResource::UsageFlags>(e.usage_flags));

      out << std::right << std::setw(3) << i << (i == 0 ? "*" : " ") << " ";
      out << std::left << std::setw(kOffsetColumn) << ToHex64(e.data_offset)
          << " ";
      out << std::right << std::setw(kSizeColumn) << e.size_bytes << " ";
      out << std::right << std::setw(kStrideColumn) << e.element_stride << " ";
      out << std::left << std::setw(kFormatColumn) << format_name << " ";
      out << std::left << std::setw(32) << usage_name << " ";
      out << std::left << std::setw(16) << ToHex64(e.content_hash) << "\n";
    }

    out << "\n  (* = sentinel/reserved index)\n";

    return {};
  }

  auto RunDumpTextures(std::ostream& out, const std::string& cooked_root_path)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(cooked_root_path);

    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kTexturesTable);
    if (!relpath) {
      return std::unexpected(
        toolkit::ToolError::Failed("textures.table not found in index"));
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::core::TextureResourceDesc>(
        table_path);

    out << "Dumping " << entries.size() << " textures in: '"
        << table_path.string() << "'\n\n";

    if (entries.empty()) {
      out << "(none)\n";
      return {};
    }

    // clang-format off
    out << "Idx  Offset              Size       Dims        Mips Layers Type           Format          Hash\n";
    out << "---- ------------------- ---------- ----------- ---- ------ -------------- --------------- ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries.at(i);
      const auto* const type_name
        = nostd::to_string(static_cast<oxygen::TextureType>(e.texture_type));
      const auto* const format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.format));
      std::ostringstream dims;
      dims << e.width << "x" << e.height;

      out << std::right << std::setw(3) << i << "  ";
      out << std::left << std::setw(kOffsetColumn) << ToHex64(e.data_offset)
          << " ";
      out << std::right << std::setw(kSizeColumn) << e.size_bytes << " ";
      out << std::left << std::setw(kDimensionsColumn) << dims.str() << " ";
      out << std::right << std::setw(4) << e.mip_levels << " ";
      out << std::right << std::setw(kLayersColumn) << e.array_layers << " ";
      out << std::left << std::setw(kTextureTypeColumn) << type_name << " ";
      out << std::left << std::setw(kFormatColumn) << format_name << " ";
      out << std::left << std::setw(16) << ToHex64(e.content_hash) << "\n";
    }

    return {};
  }

  auto RunDumpPhysics(std::ostream& out, const std::string& cooked_root_path)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(cooked_root_path);

    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPathBySuffix(inspection, "physics.table");
    if (!relpath) {
      return std::unexpected(
        toolkit::ToolError::Failed("physics.table not found in index"));
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::physics::PhysicsResourceDesc>(
        table_path);

    if (entries.empty()) {
      out << "No physics resources found in: '" << table_path.string() << "'\n";
      return {};
    }

    out << "Dumping " << entries.size() - 1 << " user physics resources ("
        << entries.size() << " total entries including sentinel) in: '"
        << table_path.string() << "'\n\n";

    out << "Idx  Offset              Size       Format                    "
           "SHA256\n";
    out << "---- ------------------- ---------- "
           "------------------------- ---------------------------------"
           "----------------\n";

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries.at(i);
      std::string_view format_name = "Unknown";
      switch (static_cast<oxygen::data::pak::physics::PhysicsResourceFormat>(
        e.format)) {
      case oxygen::data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary:
        format_name = "JoltShapeBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kJoltConstraintBinary:
        format_name = "JoltConstraintBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kJoltSoftBodySharedSettingsBinary:
        format_name = "JoltSoftBodySharedSettingsBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kJoltVehicleConstraintBinary:
        format_name = "JoltVehicleConstraintBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXConvexMeshBinary:
        format_name = "PhysXConvexMeshBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXTriangleMeshBinary:
        format_name = "PhysXTriangleMeshBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXHeightFieldBinary:
        format_name = "PhysXHeightFieldBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXConstraintBinary:
        format_name = "PhysXConstraintBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXVehicleSettingsBinary:
        format_name = "PhysXVehicleSettingsBinary";
        break;
      case oxygen::data::pak::physics::PhysicsResourceFormat::
        kPhysXSoftBodySettingsBinary:
        format_name = "PhysXSoftBodySettingsBinary";
        break;
      default:
        break;
      }

      out << std::right << std::setw(3) << i << (i == 0 ? "*" : " ") << " ";
      out << std::left << std::setw(kOffsetColumn) << ToHex64(e.data_offset)
          << " ";
      out << std::right << std::setw(kSizeColumn) << e.size_bytes << " ";
      out << std::left << std::setw(kScriptEncodingColumn) << format_name
          << " ";
      out << std::left << std::setw(kScriptHashColumn)
          << ToHexBytes(std::span<const uint8_t, 32>(e.content_hash)) << "\n";
    }

    out << "\n  (* = sentinel/reserved index)\n";
    return {};
  }

} // namespace

auto MakeBuffersCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "buffers",
    .about = "Dump buffers.table entries.",
    .dump = RunDumpBuffers,
  });
}

auto MakeTexturesCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "textures",
    .about = "Dump textures.table entries.",
    .dump = RunDumpTextures,
  });
}

auto MakePhysicsTableCommand() -> std::unique_ptr<toolkit::ToolCommand>
{
  return std::make_unique<CookedRootCommand>(CookedRootCommandSpec {
    .name = "physics-table",
    .about = "Dump physics.table entries.",
    .dump = RunDumpPhysics,
  });
}

} // namespace oxygen::content::inspection
