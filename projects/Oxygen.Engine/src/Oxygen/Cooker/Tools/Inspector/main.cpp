//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Cli.h>
#include <Oxygen/Clap/Command.h>
#include <Oxygen/Clap/CommandLineContext.h>
#include <Oxygen/Clap/Fluent/CliBuilder.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace {

using oxygen::clap::Cli;
using oxygen::clap::CliBuilder;
using oxygen::clap::Command;
using oxygen::clap::CommandBuilder;
using oxygen::clap::Option;

using oxygen::data::AssetKey;
using oxygen::data::loose_cooked::FileKind;

constexpr std::string_view kProgramName = "Oxygen.Cooker.Inspector";
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

struct DumpResourceOptions {
  std::string cooked_root;
};

struct DumpScriptOptions {
  std::string cooked_root;
};

struct DumpInputOptions {
  std::string cooked_root;
};

struct DumpPhysicsAssetsOptions {
  std::string cooked_root;
};

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
  case FileKind::kScriptBindingsTable:
    return "script-bindings.table";
  case FileKind::kScriptBindingsData:
    return "script-bindings.data";
  case FileKind::kPhysicsTable:
    return "physics.table";
  case FileKind::kPhysicsData:
    return "physics.data";
  case FileKind::kUnknown:
  default:
    return "unknown";
  }
}

auto DumpHexSha256(std::ostream& os,
  const std::span<const uint8_t, oxygen::data::loose_cooked::kSha256Size>&
    digest) -> void
{
  static constexpr char kHex[] = "0123456789abcdef";
  for (const auto b : digest) {
    os << kHex[(b >> 4) & 0x0f] << kHex[b & 0x0f];
  }
}

auto ToHex64(const uint64_t value) -> std::string
{
  std::ostringstream ss;
  ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return ss.str();
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

auto FindFileRelPathBySuffix(const oxygen::content::lc::Inspection& inspection,
  const std::string_view suffix) -> std::optional<std::string>
{
  for (const auto& entry : inspection.Files()) {
    if (entry.relpath.size() >= suffix.size()
      && entry.relpath.compare(
           entry.relpath.size() - suffix.size(), suffix.size(), suffix)
        == 0) {
      return entry.relpath;
    }
  }
  return std::nullopt;
}

template <typename T>
auto LoadPackedTable(const std::filesystem::path& table_path) -> std::vector<T>
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

auto DumpFileRecords(
  const std::span<const oxygen::content::lc::Inspection::FileEntry> entries,
  std::ostream& os) -> void
{
  if (entries.empty()) {
    os << "(none)\n";
    return;
  }

  os << "Kind             Path                                Size\n";
  os << "---------------  ----------------------------------  ----------\n";

  for (const auto& e : entries) {
    os << std::left << std::setw(15) << FileKindToString(e.kind) << "  ";
    os << std::left << std::setw(34) << e.relpath << "  ";
    os << std::right << std::setw(10) << e.size;
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

auto ValidateRootOrThrow(const std::filesystem::path& cooked_root) -> void
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::core::AssetHeader;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::render::MaterialAssetDesc;
  using oxygen::data::pak::scripting::ScriptAssetDesc;
  using oxygen::data::pak::world::SceneAssetDesc;
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  oxygen::content::lc::Inspection inspection;
  inspection.LoadFromRoot(cooked_root);

  for (const auto& asset : inspection.Assets()) {
    if (asset.descriptor_relpath.empty()) {
      throw std::runtime_error("asset descriptor path is missing");
    }

    const auto descriptor_path = cooked_root / asset.descriptor_relpath;
    if (!std::filesystem::exists(descriptor_path)) {
      throw std::runtime_error(
        "descriptor file does not exist: " + descriptor_path.generic_string());
    }

    FileStream<> stream(descriptor_path, std::ios::in);
    const auto descriptor_size_result = stream.Size();
    if (!descriptor_size_result) {
      throw std::runtime_error("failed to query descriptor file size");
    }
    const auto descriptor_size = descriptor_size_result.value();
    if (descriptor_size != asset.descriptor_size) {
      throw std::runtime_error(
        "descriptor size mismatch for " + descriptor_path.generic_string());
    }

    if (descriptor_size < sizeof(AssetHeader)) {
      throw std::runtime_error("descriptor is smaller than AssetHeader: "
        + descriptor_path.generic_string());
    }

    Reader<FileStream<>> reader(stream);
    auto pack = reader.ScopedAlignment(1);
    auto blob = reader.ReadBlob(sizeof(AssetHeader));
    if (!blob) {
      throw std::runtime_error("failed to read descriptor header");
    }

    AssetHeader header {};
    std::memcpy(&header, blob->data(), sizeof(header));

    if (header.asset_type != asset.asset_type) {
      throw std::runtime_error("descriptor header asset_type mismatch for "
        + descriptor_path.generic_string());
    }

    const auto asset_type = static_cast<AssetType>(asset.asset_type);
    size_t min_size = sizeof(AssetHeader);
    switch (asset_type) {
    case AssetType::kMaterial:
      min_size = sizeof(MaterialAssetDesc);
      break;
    case AssetType::kGeometry:
      min_size = sizeof(GeometryAssetDesc);
      break;
    case AssetType::kScene:
      min_size = sizeof(SceneAssetDesc);
      break;
    case AssetType::kScript:
      min_size = sizeof(ScriptAssetDesc);
      break;
    case AssetType::kInputAction:
      min_size = sizeof(oxygen::data::pak::input::InputActionAssetDesc);
      break;
    case AssetType::kInputMappingContext:
      min_size = sizeof(InputMappingContextAssetDesc);
      break;
    case AssetType::kPhysicsMaterial:
      min_size = sizeof(oxygen::data::pak::physics::PhysicsMaterialAssetDesc);
      break;
    case AssetType::kCollisionShape:
      min_size = sizeof(oxygen::data::pak::physics::CollisionShapeAssetDesc);
      break;
    case AssetType::kPhysicsScene:
      min_size = sizeof(oxygen::data::pak::physics::PhysicsSceneAssetDesc);
      break;
    default:
      break;
    }

    if (descriptor_size < min_size) {
      throw std::runtime_error("descriptor smaller than minimum expected size: "
        + descriptor_path.generic_string());
    }
  }
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
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const bool dump_assets = opts.assets || (!opts.assets && !opts.files);
    const bool dump_files = opts.files || (!opts.assets && !opts.files);

    std::cout << "Cooked Root : " << cooked_root.string() << "\n";
    std::cout << "Index GUID  : " << nostd::to_string(inspection.Guid())
              << "\n";

    if (dump_assets) {
      std::cout << "\nAssets (" << inspection.Assets().size() << ")\n";
      DumpAssets(inspection.Assets(), std::cout, opts.show_digests);
    }

    if (dump_files) {
      std::cout << "\nFile Records (" << inspection.Files().size() << ")\n";
      DumpFileRecords(inspection.Files(), std::cout);
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpBuffers(const DumpResourceOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kBuffersTable);
    if (!relpath) {
      std::cerr << "ERROR: buffers.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries = LoadPackedTable<oxygen::data::pak::core::BufferResourceDesc>(
      table_path);

    if (entries.empty()) {
      std::cout << "No buffers found in: '" << table_path.string() << "'\n";
      return 0;
    }

    std::cout << "Dumping " << entries.size() - 1 << " user buffers ("
              << entries.size() << " total entries including sentinel) in: '"
              << table_path.string() << "'\n\n";

    // clang-format off
    std::cout << "Idx  Offset              Size       Stride  Format          Usage Flags                      Hash\n";
    std::cout << "---- ------------------- ---------- ------ --------------- --------------------------------  ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      const auto format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.element_format));
      const auto usage_name = nostd::to_string(
        static_cast<oxygen::data::BufferResource::UsageFlags>(e.usage_flags));

      std::cout << std::right << std::setw(3) << i << (i == 0 ? "*" : " ")
                << " ";
      std::cout << std::left << std::setw(19) << ToHex64(e.data_offset) << " ";
      std::cout << std::right << std::setw(10) << e.size_bytes << " ";
      std::cout << std::right << std::setw(6) << e.element_stride << " ";
      std::cout << std::left << std::setw(15) << format_name << " ";
      std::cout << std::left << std::setw(32) << usage_name << " ";
      std::cout << std::left << std::setw(16) << ToHex64(e.content_hash)
                << "\n";
    }

    std::cout << "\n  (* = sentinel/reserved index)\n";

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpTextures(const DumpResourceOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPath(inspection, FileKind::kTexturesTable);
    if (!relpath) {
      std::cerr << "ERROR: textures.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::render::TextureResourceDesc>(
        table_path);

    std::cout << "Dumping " << entries.size() << " textures in: '"
              << table_path.string() << "'\n\n";

    if (entries.empty()) {
      std::cout << "(none)\n";
      return 0;
    }

    // clang-format off
    std::cout << "Idx  Offset              Size       Dims        Mips Layers Type           Format          Hash\n";
    std::cout << "---- ------------------- ---------- ----------- ---- ------ -------------- --------------- ----------------\n";
    // clang-format on

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      const auto type_name
        = nostd::to_string(static_cast<oxygen::TextureType>(e.texture_type));
      const auto format_name
        = nostd::to_string(static_cast<oxygen::Format>(e.format));
      std::ostringstream dims;
      dims << e.width << "x" << e.height;

      std::cout << std::right << std::setw(3) << i << "  ";
      std::cout << std::left << std::setw(19) << ToHex64(e.data_offset) << " ";
      std::cout << std::right << std::setw(10) << e.size_bytes << " ";
      std::cout << std::left << std::setw(11) << dims.str() << " ";
      std::cout << std::right << std::setw(4) << e.mip_levels << " ";
      std::cout << std::right << std::setw(6) << e.array_layers << " ";
      std::cout << std::left << std::setw(14) << type_name << " ";
      std::cout << std::left << std::setw(15) << format_name << " ";
      std::cout << std::left << std::setw(16) << ToHex64(e.content_hash)
                << "\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpPhysics(const DumpResourceOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);

  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    const auto relpath = FindFileRelPathBySuffix(inspection, "physics.table");
    if (!relpath) {
      std::cerr << "ERROR: physics.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::physics::PhysicsResourceDesc>(
        table_path);

    if (entries.empty()) {
      std::cout << "No physics resources found in: '" << table_path.string()
                << "'\n";
      return 0;
    }

    std::cout << "Dumping " << entries.size() - 1 << " user physics resources ("
              << entries.size() << " total entries including sentinel) in: '"
              << table_path.string() << "'\n\n";

    std::cout
      << "Idx  Offset              Size       Format                    Hash\n";
    std::cout << "---- ------------------- ---------- "
                 "------------------------- ----------------\n";

    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
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
      default:
        break;
      }

      std::cout << std::right << std::setw(3) << i << (i == 0 ? "*" : " ")
                << " ";
      std::cout << std::left << std::setw(19) << ToHex64(e.data_offset) << " ";
      std::cout << std::right << std::setw(10) << e.size_bytes << " ";
      std::cout << std::left << std::setw(25) << format_name << " ";
      std::cout << std::left << std::setw(16) << ToHex64(e.content_hash)
                << "\n";
    }

    std::cout << "\n  (* = sentinel/reserved index)\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto ReadFixedString(const char* bytes, size_t max_len) -> std::string
{
  size_t len = 0;
  for (; len < max_len; ++len) {
    if (bytes[len] == '\0') {
      break;
    }
  }
  return std::string(bytes, len);
}

auto FormatScriptParamValue(
  const oxygen::data::pak::scripting::ScriptParamRecord& record) -> std::string
{
  using oxygen::data::pak::scripting::ScriptParamType;
  switch (record.type) {
  case ScriptParamType::kBool:
    return record.value.as_bool ? "true" : "false";
  case ScriptParamType::kInt32:
    return std::to_string(record.value.as_int32);
  case ScriptParamType::kFloat:
    return std::to_string(record.value.as_float);
  case ScriptParamType::kString:
    return "\"" + ReadFixedString(record.value.as_string, 60) + "\"";
  case ScriptParamType::kVec2:
    return fmt::format(
      "({}, {})", record.value.as_vec[0], record.value.as_vec[1]);
  case ScriptParamType::kVec3:
    return fmt::format("({}, {}, {})", record.value.as_vec[0],
      record.value.as_vec[1], record.value.as_vec[2]);
  case ScriptParamType::kVec4:
    return fmt::format("({}, {}, {}, {})", record.value.as_vec[0],
      record.value.as_vec[1], record.value.as_vec[2], record.value.as_vec[3]);
  case ScriptParamType::kNone:
  default:
    return "<none>";
  }
}

auto ReadDescriptorBytes(const std::filesystem::path& descriptor_path)
  -> std::vector<std::byte>
{
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  FileStream<> stream(descriptor_path, std::ios::in);
  const auto size_result = stream.Size();
  if (!size_result) {
    throw std::runtime_error("failed to query descriptor size");
  }
  const auto size_bytes = size_result.value();
  if (size_bytes == 0) {
    return {};
  }

  Reader<FileStream<>> reader(stream);
  auto pack = reader.ScopedAlignment(1);
  auto blob = reader.ReadBlob(size_bytes);
  if (!blob) {
    throw std::runtime_error("failed to read descriptor");
  }
  return *blob;
}

template <typename T>
auto ReadStructAt(const std::vector<std::byte>& bytes, const size_t offset)
  -> std::optional<T>
{
  if (offset > bytes.size() || sizeof(T) > (bytes.size() - offset)) {
    return std::nullopt;
  }
  T value {};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

auto ParseStringFromTable(
  const std::span<const char> table, const uint32_t offset) -> std::string
{
  if (offset >= table.size()) {
    return "<invalid>";
  }

  const auto* const base = table.data() + offset;
  const auto max_len = table.size() - offset;
  size_t len = 0;
  for (; len < max_len; ++len) {
    if (base[len] == '\0') {
      break;
    }
  }
  return std::string(base, len);
}

auto RunDumpInputActions(const DumpInputOptions& opts) -> int
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionAssetDesc;

  const std::filesystem::path cooked_root(opts.cooked_root);
  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type != static_cast<uint8_t>(AssetType::kInputAction)) {
        continue;
      }

      ++count;
      std::cout << "InputAction key='" << oxygen::data::to_string(asset.key)
                << "' desc='" << asset.descriptor_relpath << "'\n";

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));
      if (descriptor.size() < sizeof(InputActionAssetDesc)) {
        std::cout << "  ! descriptor too small\n";
        continue;
      }

      const auto desc = ReadStructAt<InputActionAssetDesc>(descriptor, 0);
      if (!desc) {
        std::cout << "  ! failed to decode descriptor\n";
        continue;
      }

      std::cout << "  name='"
                << ReadFixedString(desc->header.name, sizeof(desc->header.name))
                << "' value_type=" << static_cast<uint32_t>(desc->value_type)
                << " flags=" << nostd::to_string(desc->flags) << "\n";
    }

    if (count == 0) {
      std::cout << "(no input actions)\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpInputMappings(const DumpInputOptions& opts) -> int
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::input::InputActionMappingRecord;
  using oxygen::data::pak::input::InputMappingContextAssetDesc;
  using oxygen::data::pak::input::InputTriggerAuxRecord;
  using oxygen::data::pak::input::InputTriggerRecord;

  const std::filesystem::path cooked_root(opts.cooked_root);
  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type
        != static_cast<uint8_t>(AssetType::kInputMappingContext)) {
        continue;
      }
      ++count;

      std::cout << "InputMappingContext key='"
                << oxygen::data::to_string(asset.key) << "' desc='"
                << asset.descriptor_relpath << "'\n";

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));
      if (descriptor.size() < sizeof(InputMappingContextAssetDesc)) {
        std::cout << "  ! descriptor too small\n";
        continue;
      }

      const auto desc
        = ReadStructAt<InputMappingContextAssetDesc>(descriptor, 0);
      if (!desc) {
        std::cout << "  ! failed to decode descriptor\n";
        continue;
      }

      std::cout << "  name='"
                << ReadFixedString(desc->header.name, sizeof(desc->header.name))
                << "' flags=" << nostd::to_string(desc->flags)
                << " mappings=" << desc->mappings.count
                << " triggers=" << desc->triggers.count
                << " aux=" << desc->trigger_aux.count
                << " strings=" << desc->strings.count << "\n";

      std::span<const char> string_table {};
      const auto string_off = static_cast<size_t>(desc->strings.offset);
      const auto string_size = static_cast<size_t>(desc->strings.count);
      if (string_size > 0 && string_off <= descriptor.size()
        && string_size <= (descriptor.size() - string_off)) {
        string_table = std::span<const char>(
          reinterpret_cast<const char*>(descriptor.data() + string_off),
          string_size);
      }

      const auto mapping_count = static_cast<size_t>(desc->mappings.count);
      const auto mapping_start = static_cast<size_t>(desc->mappings.offset);
      const auto mapping_size = sizeof(InputActionMappingRecord);
      for (size_t i = 0; i < mapping_count; ++i) {
        const auto rec = ReadStructAt<InputActionMappingRecord>(
          descriptor, mapping_start + i * mapping_size);
        if (!rec) {
          std::cout << "    ! mapping[" << i << "] decode failed\n";
          continue;
        }

        std::cout << "    mapping[" << i << "] action="
                  << oxygen::data::to_string(rec->action_asset_key) << " slot='"
                  << ParseStringFromTable(string_table, rec->slot_name_offset)
                  << "' triggers=[" << rec->trigger_start_index << ","
                  << (rec->trigger_start_index + rec->trigger_count)
                  << ") flags=" << nostd::to_string(rec->flags) << "\n";
      }

      const auto trigger_count = static_cast<size_t>(desc->triggers.count);
      const auto trigger_start = static_cast<size_t>(desc->triggers.offset);
      const auto trigger_size = sizeof(InputTriggerRecord);
      for (size_t i = 0; i < trigger_count; ++i) {
        const auto rec = ReadStructAt<InputTriggerRecord>(
          descriptor, trigger_start + i * trigger_size);
        if (!rec) {
          std::cout << "    ! trigger[" << i << "] decode failed\n";
          continue;
        }
        std::cout << "    trigger[" << i
                  << "] type=" << nostd::to_string(rec->type)
                  << " behavior=" << nostd::to_string(rec->behavior)
                  << " threshold=" << rec->actuation_threshold << " aux=["
                  << rec->aux_start_index << ","
                  << (rec->aux_start_index + rec->aux_count) << ")\n";
      }

      const auto aux_count = static_cast<size_t>(desc->trigger_aux.count);
      const auto aux_start = static_cast<size_t>(desc->trigger_aux.offset);
      const auto aux_size = sizeof(InputTriggerAuxRecord);
      for (size_t i = 0; i < aux_count; ++i) {
        const auto rec = ReadStructAt<InputTriggerAuxRecord>(
          descriptor, aux_start + i * aux_size);
        if (!rec) {
          std::cout << "    ! trigger_aux[" << i << "] decode failed\n";
          continue;
        }
        std::cout << "    trigger_aux[" << i << "] action="
                  << oxygen::data::to_string(rec->action_asset_key)
                  << " completion=0x" << std::hex << rec->completion_states
                  << std::dec << " time_ns=" << rec->time_to_complete_ns
                  << "\n";
      }
    }

    if (count == 0) {
      std::cout << "(no input mapping contexts)\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpPhysicsAssets(const DumpPhysicsAssetsOptions& opts) -> int
{
  using oxygen::data::AssetType;
  using oxygen::data::pak::physics::CollisionShapeAssetDesc;
  using oxygen::data::pak::physics::PhysicsComponentTableDesc;
  using oxygen::data::pak::physics::PhysicsMaterialAssetDesc;
  using oxygen::data::pak::physics::PhysicsSceneAssetDesc;

  const std::filesystem::path cooked_root(opts.cooked_root);
  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    size_t count = 0;
    for (const auto& asset : inspection.Assets()) {
      const auto type = static_cast<AssetType>(asset.asset_type);
      if (type != AssetType::kPhysicsMaterial
        && type != AssetType::kCollisionShape
        && type != AssetType::kPhysicsScene) {
        continue;
      }
      ++count;

      const auto descriptor = ReadDescriptorBytes(
        cooked_root / std::filesystem::path(asset.descriptor_relpath));

      if (type == AssetType::kPhysicsMaterial) {
        std::cout << "PhysicsMaterial key='"
                  << oxygen::data::to_string(asset.key) << "' desc='"
                  << asset.descriptor_relpath << "'\n";
        if (descriptor.size() < sizeof(PhysicsMaterialAssetDesc)) {
          std::cout << "  ! descriptor too small\n";
          continue;
        }
        const auto desc = ReadStructAt<PhysicsMaterialAssetDesc>(descriptor, 0);
        if (!desc) {
          std::cout << "  ! failed to decode descriptor\n";
          continue;
        }
        std::cout << "  name='"
                  << ReadFixedString(
                       desc->header.name, sizeof(desc->header.name))
                  << "' friction=" << desc->friction
                  << " restitution=" << desc->restitution
                  << " density=" << desc->density << "\n";
        continue;
      }

      if (type == AssetType::kCollisionShape) {
        std::cout << "CollisionShape key='"
                  << oxygen::data::to_string(asset.key) << "' desc='"
                  << asset.descriptor_relpath << "'\n";
        if (descriptor.size() < sizeof(CollisionShapeAssetDesc)) {
          std::cout << "  ! descriptor too small\n";
          continue;
        }
        const auto desc = ReadStructAt<CollisionShapeAssetDesc>(descriptor, 0);
        if (!desc) {
          std::cout << "  ! failed to decode descriptor\n";
          continue;
        }
        std::cout << "  name='"
                  << ReadFixedString(
                       desc->header.name, sizeof(desc->header.name))
                  << "' shape_type=" << static_cast<uint32_t>(desc->shape_type)
                  << " local_pos=(" << desc->local_position[0] << ","
                  << desc->local_position[1] << "," << desc->local_position[2]
                  << ") local_scale=(" << desc->local_scale[0] << ","
                  << desc->local_scale[1] << "," << desc->local_scale[2]
                  << ") is_sensor=" << desc->is_sensor << " material_asset_key="
                  << oxygen::data::to_string(desc->material_asset_key)
                  << " cooked_ref.index="
                  << desc->cooked_shape_ref.resource_index
                  << " cooked_ref.type="
                  << static_cast<uint32_t>(desc->cooked_shape_ref.payload_type)
                  << "\n";
        continue;
      }

      std::cout << "PhysicsScene key='" << oxygen::data::to_string(asset.key)
                << "' desc='" << asset.descriptor_relpath << "'\n";
      if (descriptor.size() < sizeof(PhysicsSceneAssetDesc)) {
        std::cout << "  ! descriptor too small\n";
        continue;
      }
      const auto desc = ReadStructAt<PhysicsSceneAssetDesc>(descriptor, 0);
      if (!desc) {
        std::cout << "  ! failed to decode descriptor\n";
        continue;
      }
      std::cout << "  name='"
                << ReadFixedString(desc->header.name, sizeof(desc->header.name))
                << "' target_scene_key="
                << oxygen::data::to_string(desc->target_scene_key)
                << " target_node_count=" << desc->target_node_count
                << " table_count=" << desc->component_table_count
                << " table_dir_offset="
                << ToHex64(desc->component_table_directory_offset) << "\n";

      const size_t dir_offset
        = static_cast<size_t>(desc->component_table_directory_offset);
      const size_t dir_count = static_cast<size_t>(desc->component_table_count);
      const size_t dir_bytes = dir_count * sizeof(PhysicsComponentTableDesc);
      if (dir_count == 0) {
        continue;
      }
      if (dir_offset > descriptor.size()
        || dir_bytes > (descriptor.size() - dir_offset)) {
        std::cout << "  ! component table directory out of bounds\n";
        continue;
      }

      uint64_t total_bindings = 0;
      for (size_t i = 0; i < dir_count; ++i) {
        const auto entry = ReadStructAt<PhysicsComponentTableDesc>(
          descriptor, dir_offset + i * sizeof(PhysicsComponentTableDesc));
        if (!entry) {
          std::cout << "    ! table[" << i << "] decode failed\n";
          continue;
        }
        total_bindings += entry->table.count;
        std::cout << "    table[" << i << "] type=0x" << std::hex
                  << static_cast<uint32_t>(entry->binding_type) << std::dec
                  << " count=" << entry->table.count
                  << " entry_size=" << entry->table.entry_size
                  << " offset=" << ToHex64(entry->table.offset) << "\n";
      }
      std::cout << "    total_bindings=" << total_bindings << "\n";
    }

    if (count == 0) {
      std::cout << "(no physics assets)\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpScriptSlots(const DumpScriptOptions& opts) -> int
{
  const std::filesystem::path cooked_root(opts.cooked_root);
  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    auto relpath = FindFileRelPathBySuffix(inspection, "script-bindings.table");
    if (!relpath) {
      std::cerr << "ERROR: script-bindings.table not found in index\n";
      return 2;
    }

    const auto table_path = cooked_root / *relpath;
    auto entries
      = LoadPackedTable<oxygen::data::pak::scripting::ScriptSlotRecord>(
        table_path);
    if (entries.empty()) {
      std::cout << "No script slots found in: '" << table_path.string()
                << "'\n";
      return 0;
    }

    std::cout << "Dumping " << entries.size() << " script slots in: '"
              << table_path.string() << "'\n\n";
    std::cout << "Idx  Script Asset Key                        ParamOffset     "
                 "     Count  ExecOrder  Flags\n";
    std::cout << "---- -------------------------------------- "
                 "------------------- ------ ---------- ----------\n";
    for (size_t i = 0; i < entries.size(); ++i) {
      const auto& e = entries[i];
      std::cout << std::right << std::setw(3) << i << "  " << std::left
                << std::setw(38) << oxygen::data::to_string(e.script_asset_key)
                << " " << std::left << std::setw(19)
                << ToHex64(e.params_array_offset) << " " << std::right
                << std::setw(6) << e.params_count << " " << std::right
                << std::setw(10) << e.execution_order << " " << std::left
                << std::setw(10) << static_cast<uint32_t>(e.flags) << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto RunDumpScriptParams(const DumpScriptOptions& opts) -> int
{
  using oxygen::serio::FileStream;
  using oxygen::serio::Reader;

  const std::filesystem::path cooked_root(opts.cooked_root);
  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    auto slots_relpath
      = FindFileRelPathBySuffix(inspection, "script-bindings.table");
    auto data_relpath
      = FindFileRelPathBySuffix(inspection, "script-bindings.data");
    if (!slots_relpath) {
      std::cerr << "ERROR: script-bindings.table not found in index\n";
      return 2;
    }
    if (!data_relpath) {
      std::cerr << "ERROR: script-bindings.data not found in index\n";
      return 2;
    }

    const auto slots_path = cooked_root / *slots_relpath;
    const auto data_path = cooked_root / *data_relpath;
    auto slots
      = LoadPackedTable<oxygen::data::pak::scripting::ScriptSlotRecord>(
        slots_path);
    if (slots.empty()) {
      std::cout << "(no script slots)\n";
      return 0;
    }

    FileStream<> data_stream(data_path, std::ios::in);
    Reader<FileStream<>> reader(data_stream);
    auto pack = reader.ScopedAlignment(1);

    for (size_t i = 0; i < slots.size(); ++i) {
      const auto& slot = slots[i];
      std::cout << "Slot[" << i
                << "] key=" << oxygen::data::to_string(slot.script_asset_key)
                << " params_count=" << slot.params_count
                << " params_offset=" << ToHex64(slot.params_array_offset)
                << "\n";
      if (slot.params_count == 0) {
        continue;
      }

      auto seek_result
        = reader.Seek(static_cast<size_t>(slot.params_array_offset));
      if (!seek_result) {
        std::cout << "  ! cannot seek to params offset (skipping)\n";
        continue;
      }

      const size_t bytes_to_read = static_cast<size_t>(slot.params_count)
        * sizeof(oxygen::data::pak::scripting::ScriptParamRecord);
      auto blob = reader.ReadBlob(bytes_to_read);
      if (!blob) {
        std::cout << "  ! failed to read params blob (skipping)\n";
        continue;
      }

      for (uint32_t pi = 0; pi < slot.params_count; ++pi) {
        oxygen::data::pak::scripting::ScriptParamRecord record {};
        std::memcpy(&record,
          blob->data() + static_cast<size_t>(pi) * sizeof(record),
          sizeof(record));
        const auto key = ReadFixedString(record.key, 64);
        std::cout << "    [" << pi << "] "
                  << "key='" << key << "' "
                  << "type=" << static_cast<uint32_t>(record.type)
                  << " value=" << FormatScriptParamValue(record) << "\n";
      }
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: " << ex.what() << "\n";
    return 2;
  }
}

auto BuildCli(ValidateOptions& validate_opts, DumpOptions& dump_opts,
  DumpResourceOptions& buffers_opts, DumpResourceOptions& textures_opts,
  DumpResourceOptions& physics_opts, DumpScriptOptions& script_slots_opts,
  DumpScriptOptions& script_params_opts, DumpInputOptions& input_actions_opts,
  DumpInputOptions& input_mappings_opts,
  DumpPhysicsAssetsOptions& physics_assets_opts) -> std::unique_ptr<Cli>
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
    = CommandBuilder("validate")
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
    = CommandBuilder("index")
        .About("Dump container.index.bin contents (validated).")
        .WithPositionalArguments(dump_root)
        .WithOption(std::move(dump_assets))
        .WithOption(std::move(dump_files))
        .WithOption(std::move(dump_digests));

  auto buffers_root = Option::Positional("cooked_root")
                        .About("Loose cooked root directory")
                        .Required()
                        .WithValue<std::string>()
                        .StoreTo(&buffers_opts.cooked_root)
                        .Build();

  const std::shared_ptr<Command> buffers_cmd
    = CommandBuilder("buffers")
        .About("Dump buffers.table entries.")
        .WithPositionalArguments(buffers_root);

  auto textures_root = Option::Positional("cooked_root")
                         .About("Loose cooked root directory")
                         .Required()
                         .WithValue<std::string>()
                         .StoreTo(&textures_opts.cooked_root)
                         .Build();

  const std::shared_ptr<Command> textures_cmd
    = CommandBuilder("textures")
        .About("Dump textures.table entries.")
        .WithPositionalArguments(textures_root);

  auto physics_root = Option::Positional("cooked_root")
                        .About("Loose cooked root directory")
                        .Required()
                        .WithValue<std::string>()
                        .StoreTo(&physics_opts.cooked_root)
                        .Build();

  const std::shared_ptr<Command> physics_table_cmd
    = CommandBuilder("physics-table")
        .About("Dump physics.table entries.")
        .WithPositionalArguments(physics_root);

  auto script_slots_root = Option::Positional("cooked_root")
                             .About("Loose cooked root directory")
                             .Required()
                             .WithValue<std::string>()
                             .StoreTo(&script_slots_opts.cooked_root)
                             .Build();

  const std::shared_ptr<Command> script_slots_cmd
    = CommandBuilder("script-slots")
        .About("Dump script-bindings.table slot entries.")
        .WithPositionalArguments(script_slots_root);

  auto script_params_root = Option::Positional("cooked_root")
                              .About("Loose cooked root directory")
                              .Required()
                              .WithValue<std::string>()
                              .StoreTo(&script_params_opts.cooked_root)
                              .Build();

  const std::shared_ptr<Command> script_params_cmd
    = CommandBuilder("script-params")
        .About(
          "Dump script param arrays referenced by script-bindings.table slots.")
        .WithPositionalArguments(script_params_root);

  auto input_actions_root = Option::Positional("cooked_root")
                              .About("Loose cooked root directory")
                              .Required()
                              .WithValue<std::string>()
                              .StoreTo(&input_actions_opts.cooked_root)
                              .Build();

  const std::shared_ptr<Command> input_actions_cmd
    = CommandBuilder("input-actions")
        .About("Dump input action descriptors.")
        .WithPositionalArguments(input_actions_root);

  auto input_mappings_root = Option::Positional("cooked_root")
                               .About("Loose cooked root directory")
                               .Required()
                               .WithValue<std::string>()
                               .StoreTo(&input_mappings_opts.cooked_root)
                               .Build();

  const std::shared_ptr<Command> input_mappings_cmd
    = CommandBuilder("input-mappings")
        .About("Dump input mapping context descriptors.")
        .WithPositionalArguments(input_mappings_root);

  auto physics_assets_root = Option::Positional("cooked_root")
                               .About("Loose cooked root directory")
                               .Required()
                               .WithValue<std::string>()
                               .StoreTo(&physics_assets_opts.cooked_root)
                               .Build();

  const std::shared_ptr<Command> physics_assets_cmd
    = CommandBuilder("physics")
        .About("Dump physics asset descriptors.")
        .WithPositionalArguments(physics_assets_root);

  return CliBuilder()
    .ProgramName(std::string(kProgramName))
    .Version(std::string(kVersion))
    .About(
      "Inspect and validate loose cooked content roots (filesystem-backed).")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(validate_cmd)
    .WithCommand(dump_cmd)
    .WithCommand(buffers_cmd)
    .WithCommand(textures_cmd)
    .WithCommand(physics_table_cmd)
    .WithCommand(script_slots_cmd)
    .WithCommand(script_params_cmd)
    .WithCommand(input_actions_cmd)
    .WithCommand(input_mappings_cmd)
    .WithCommand(physics_assets_cmd)
    .Build();
}

} // namespace

auto main(int argc, char** argv) -> int
{
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = true;
  loguru::g_preamble_header = false;
  loguru::g_global_verbosity = loguru::Verbosity_OFF;

  loguru::init(argc, const_cast<const char**>(argv));
  loguru::set_thread_name("main");

  int exit_code = 0;
  try {
    ValidateOptions validate_opts;
    DumpOptions dump_opts;
    DumpResourceOptions buffers_opts;
    DumpResourceOptions textures_opts;
    DumpResourceOptions physics_opts;
    DumpScriptOptions script_slots_opts;
    DumpScriptOptions script_params_opts;
    DumpInputOptions input_actions_opts;
    DumpInputOptions input_mappings_opts;
    DumpPhysicsAssetsOptions physics_assets_opts;

    const auto cli = BuildCli(validate_opts, dump_opts, buffers_opts,
      textures_opts, physics_opts, script_slots_opts, script_params_opts,
      input_actions_opts, input_mappings_opts, physics_assets_opts);
    const auto context = cli->Parse(argc, const_cast<const char**>(argv));

    const auto command_path = context.active_command->PathAsString();
    const auto& ovm = context.ovm;

    if (command_path == Command::VERSION || command_path == Command::HELP
      || ovm.HasOption(Command::HELP)) {
      exit_code = 0;
    } else if (command_path == "validate") {
      exit_code = RunValidate(validate_opts);
    } else if (command_path == "index") {
      exit_code = RunDumpIndex(dump_opts);
    } else if (command_path == "buffers") {
      exit_code = RunDumpBuffers(buffers_opts);
    } else if (command_path == "textures") {
      exit_code = RunDumpTextures(textures_opts);
    } else if (command_path == "physics-table") {
      exit_code = RunDumpPhysics(physics_opts);
    } else if (command_path == "script-slots") {
      exit_code = RunDumpScriptSlots(script_slots_opts);
    } else if (command_path == "script-params") {
      exit_code = RunDumpScriptParams(script_params_opts);
    } else if (command_path == "input-actions") {
      exit_code = RunDumpInputActions(input_actions_opts);
    } else if (command_path == "input-mappings") {
      exit_code = RunDumpInputMappings(input_mappings_opts);
    } else if (command_path == "physics") {
      exit_code = RunDumpPhysicsAssets(physics_assets_opts);
    } else {
      std::cerr << "ERROR: Unknown command\n";
      exit_code = 1;
    }
  } catch (const std::exception& /*ex*/) {
    // The error is already printed by the CLI parser.
    exit_code = 3;
  }

  loguru::flush();
  loguru::g_global_verbosity = loguru::Verbosity_OFF;
  loguru::shutdown();

  return exit_code;
}
