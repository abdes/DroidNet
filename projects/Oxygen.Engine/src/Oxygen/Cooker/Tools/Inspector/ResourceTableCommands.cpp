//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/CookedRootCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/DumpFormat.h>
#include <Oxygen/Cooker/Tools/Inspector/ResourceTableCommands.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::inspection {

namespace {

  constexpr int kOffsetColumn = 19;
  constexpr int kStrideColumn = 6;
  constexpr int kFormatColumn = 15;
  constexpr int kDimensionsColumn = 11;
  constexpr int kLayersColumn = 6;
  constexpr int kTextureTypeColumn = 14;
  constexpr int kScriptEncodingColumn = 25;
  constexpr int kScriptHashColumn = 66;

  //! Every row of the table of `kind`, row zero included.
  template <typename T>
  auto LoadRows(const cooked::CookedContent& content,
    const oxygen::data::ResourceKind kind) -> std::vector<T>
  {
    const auto& table = content.ResourceTable(kind);
    auto entries = std::vector<T> {};
    entries.reserve(table.RowCount());
    for (uint32_t row = 0U; row < table.RowCount(); ++row) {
      entries.push_back(table.Row<T>(ResourceIndexT { row }));
    }
    return entries;
  }

  //! Where the rows of the table of `kind` are stored, for the report header.
  auto TableLocation(const cooked::CookedContent& content,
    const oxygen::data::ResourceKind kind, const std::filesystem::path& root)
    -> std::string
  {
    const auto& table = content.ResourceTable(kind);
    return table.RowCount() > 0U ? table.RowsStored().file.string()
                                 : root.string();
  }

  auto RunDumpBuffers(std::ostream& out, const std::string& cooked_root_path)
    -> toolkit::ToolResult
  {
    const std::filesystem::path cooked_root(cooked_root_path);

    const auto content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);

    constexpr auto kind = oxygen::data::ResourceKind::kBuffer;
    const auto table_path = TableLocation(content, kind, cooked_root);
    const auto entries
      = LoadRows<oxygen::data::pak::core::BufferResourceDesc>(content, kind);

    if (entries.empty()) {
      out << "No buffers found in: '" << table_path << "'\n";
      return {};
    }

    out << "Dumping " << entries.size() - 1 << " user buffers ("
        << entries.size() << " total entries including sentinel) in: '"
        << table_path << "'\n\n";

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

    const auto content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);

    constexpr auto kind = oxygen::data::ResourceKind::kTexture;
    const auto table_path = TableLocation(content, kind, cooked_root);
    const auto entries
      = LoadRows<oxygen::data::pak::core::TextureResourceDesc>(content, kind);

    out << "Dumping " << entries.size() << " textures in: '" << table_path
        << "'\n\n";

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

    const auto content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root);

    constexpr auto kind = oxygen::data::ResourceKind::kPhysics;
    const auto table_path = TableLocation(content, kind, cooked_root);
    const auto entries
      = LoadRows<oxygen::data::pak::physics::PhysicsResourceDesc>(
        content, kind);

    if (entries.empty()) {
      out << "No physics resources found in: '" << table_path << "'\n";
      return {};
    }

    out << "Dumping " << entries.size() - 1 << " user physics resources ("
        << entries.size() << " total entries including sentinel) in: '"
        << table_path << "'\n\n";

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
          << "0x"
          + base::ToHexString(std::as_bytes(std::span { e.content_hash }))
          << "\n";
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
