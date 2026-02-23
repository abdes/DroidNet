//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>

#include "AssetDumpers.h"
#include "DumpContext.h"
#include "PakFileDumper.h"
#include "PrintUtils.h"

using namespace PrintUtils;

using namespace oxygen::content;
using namespace oxygen::data;
using namespace oxygen::data::pak;

//=== Resource Data Access ===================================================//

/*!
 Helper function to print resource data (actual buffer/texture blob content).
 This is separate from asset descriptors - it reads the raw binary data
 that buffers and textures point to.
 */
auto PrintResourceData(std::span<const uint8_t> data,
  const std::string& resource_type, size_t max_bytes = 256) -> void
{
  try {
    size_t bytes_to_read
      = (std::min)(static_cast<size_t>(data.size()), max_bytes);
    std::cout << "        " << resource_type << " Data Preview ("
              << bytes_to_read << " of " << data.size() << " bytes):\n";
    PrintUtils::HexDump(data.data(), bytes_to_read, max_bytes);

  } catch (const std::exception& ex) {
    std::cout << "        Failed to read " << resource_type
              << " data: " << ex.what() << "\n";
  }
}

//=== PAK Structure Dumping Functions ========================================//

auto PrintResourceRegion(
  const std::string& name, uint64_t offset, uint64_t size) -> void
{
  fmt::print("    {:<16}offset=0x{:08x}, size={} bytes{}\n", name + ":", offset,
    size, size == 0 ? " (empty)" : "");
}

auto PrintResourceTable(const std::string& name, uint64_t offset,
  uint32_t count, uint32_t entry_size) -> void
{
  fmt::print("    {:<16}offset=0x{:08x}, count={}, entry_size={} bytes{}\n",
    name + ":", offset, count, entry_size, count == 0 ? " (empty)" : "");
}

template <typename T> auto ToHexString(T value) -> std::string
{
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

namespace {

auto ReadPakFooter(const std::filesystem::path& pak_path)
  -> std::optional<PakFooter>
{
  std::ifstream file(pak_path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }

  file.seekg(0, std::ios::end);
  const auto file_size = file.tellg();
  if (file_size < static_cast<std::streamoff>(sizeof(PakFooter))) {
    return std::nullopt;
  }

  file.seekg(file_size - static_cast<std::streamoff>(sizeof(PakFooter)));
  PakFooter footer {};
  file.read(reinterpret_cast<char*>(&footer), sizeof(footer));
  if (!file) {
    return std::nullopt;
  }
  return footer;
}

auto FooterMagicOk(const PakFooter& footer) -> bool
{
  return std::ranges::equal(
    std::span { footer.footer_magic }, oxygen::data::pak::kPakFooterMagic);
}

auto TexturePackingPolicyName(uint8_t policy) -> std::string_view
{
  switch (policy) {
  case 1:
    return "D3D12";
  case 2:
    return "TightPacked";
  default:
    return "Unknown";
  }
}

auto PrintV4TexturePayloadSummary(std::span<const uint8_t> payload, int indent)
  -> void
{
  using namespace PrintUtils;
  if (payload.size() < sizeof(v4::TexturePayloadHeader)) {
    Field("Texture Payload", "Too small to contain v4 header", indent);
    return;
  }

  v4::TexturePayloadHeader header {};
  std::memcpy(&header, payload.data(), sizeof(header));
  if (header.magic != v4::kTexturePayloadMagic) {
    Field("Texture Payload", "Missing v4 magic (expected 'OTX1')", indent);
    return;
  }

  Field("Payload Magic", "OTX1", indent);
  Field("Packing Policy",
    fmt::format("{} ({})", static_cast<int>(header.packing_policy),
      TexturePackingPolicyName(header.packing_policy)),
    indent);
  Field("Flags", fmt::format("0x{:02x}", header.flags), indent);
  Field("Subresources", std::to_string(header.subresource_count), indent);
  Field(
    "Total Payload Size", std::to_string(header.total_payload_size), indent);
  Field("Layouts Offset", ToHexString(header.layouts_offset_bytes), indent);
  Field("Data Offset", ToHexString(header.data_offset_bytes), indent);
  Field("Payload Content Hash", ToHexString(header.content_hash), indent);

  if (header.total_payload_size != payload.size()) {
    Field("Payload Size Check",
      fmt::format("mismatch (header={} actual={})", header.total_payload_size,
        payload.size()),
      indent);
  }

  const auto layouts_offset = static_cast<size_t>(header.layouts_offset_bytes);
  const auto data_offset = static_cast<size_t>(header.data_offset_bytes);
  const auto layout_count = static_cast<size_t>(header.subresource_count);
  const auto layouts_bytes = layout_count * sizeof(v4::SubresourceLayout);

  if (layouts_offset + layouts_bytes > payload.size()) {
    Field("Layouts", "Out of bounds", indent);
    return;
  }
  if (data_offset > payload.size()) {
    Field("Data Section", "Out of bounds", indent);
    return;
  }

  constexpr size_t kMaxLayoutsToPrint = 16;
  const auto count = (std::min)(layout_count, kMaxLayoutsToPrint);
  if (count == 0) {
    return;
  }

  std::cout << std::string(static_cast<size_t>(indent), ' ')
            << "Subresource Layouts (" << count
            << (layout_count > count ? " shown" : "") << "):\n";
  for (size_t i = 0; i < count; ++i) {
    v4::SubresourceLayout layout {};
    const auto offset = layouts_offset + i * sizeof(v4::SubresourceLayout);
    std::memcpy(&layout, payload.data() + offset, sizeof(layout));
    const auto abs_data_offset
      = data_offset + static_cast<size_t>(layout.offset_bytes);
    const auto end = abs_data_offset + static_cast<size_t>(layout.size_bytes);
    const auto within_bounds = end <= payload.size();

    std::cout << std::string(static_cast<size_t>(indent), ' ') << "  [" << i
              << "] offset=" << ToHexString(layout.offset_bytes)
              << " row_pitch=" << layout.row_pitch_bytes
              << " size=" << layout.size_bytes
              << (within_bounds ? "" : " (oob)") << "\n";
  }
  if (layout_count > kMaxLayoutsToPrint) {
    std::cout << std::string(static_cast<size_t>(indent), ' ') << "  ... ("
              << (layout_count - kMaxLayoutsToPrint) << " more)\n";
  }
}

} // namespace

//=== ResourceTableDumper Interface and Registry ===========================//

class ResourceTableDumper {
public:
  virtual ~ResourceTableDumper() = default;
  virtual auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<>
    = 0;
};

class BufferResourceTableDumper : public ResourceTableDumper {
public:
  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<> override
  {
    if (!ctx.show_resources) {
      co_return;
    }
    if (!pak.HasTableOf<BufferResource>()) {
      std::cout << "    No buffer resource table present\n\n";
      co_return;
    }
    using namespace PrintUtils;
    SubSeparator("BUFFER RESOURCES");
    auto& buffers_table = pak.BuffersTable();
    size_t buffer_count = buffers_table.Size();
    if (buffer_count > 0) {
      Field("Buffer Count", buffer_count - 1);
      Field("Total Table Entries (inc. sentinel)", buffer_count);
    } else {
      Field("Buffer Count", 0);
    }
    if ((ctx.verbose || ctx.show_resource_data) && buffer_count > 0) {
      std::cout << "    Buffer entries:\n";
      for (size_t i = 0; i < (std::min)(buffer_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<BufferResource>(
            pak, static_cast<uint32_t>(i));
          auto buffer_resource
            = co_await asset_loader.LoadResourceAsync<BufferResource>(key);
          if (buffer_resource) {
            std::cout << "      [" << i << "] Buffer Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(
              "Data Offset", ToHexString(buffer_resource->GetDataOffset()), 8);
            Field("Data Size",
              std::to_string(buffer_resource->GetDataSize()) + " bytes", 8);
            Field("Element Stride",
              std::to_string(buffer_resource->GetElementStride()), 8);
            Field("Element Format",
              nostd::to_string(buffer_resource->GetElementFormat()), 8);
            Field("Usage Flags",
              nostd::to_string(buffer_resource->GetUsageFlags()), 8);
            Field("Content Hash",
              ToHexString(buffer_resource->GetContentHash()), 8);
            std::string buffer_type;
            if (buffer_resource->IsRaw()) {
              buffer_type = "Raw";
            } else if (buffer_resource->IsStructured()) {
              buffer_type = "Structured";
            } else if (buffer_resource->IsFormatted()) {
              buffer_type = "Formatted";
            } else {
              buffer_type = "Unknown";
            }
            Field("Buffer Type", buffer_type, 8);
            if (ctx.show_resource_data) {
              PrintResourceData(
                buffer_resource->GetData(), "Buffer", ctx.max_data_bytes);
            }
          } else {
            std::cout << "      [" << i << "] Failed to load buffer resource\n";
          }
        } catch (const std::exception& ex) {
          std::cout << "      [" << i << "] Error loading buffer: " << ex.what()
                    << "\n";
        }
      }
      if (buffer_count > 20) {
        std::cout << "      ... (" << (buffer_count - 20) << " more buffers)\n";
      }
    }
    std::cout << "\n";
    co_return;
  }
};

class TextureResourceTableDumper : public ResourceTableDumper {
public:
  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<> override
  {
    if (!ctx.show_resources) {
      co_return;
    }
    if (!pak.HasTableOf<TextureResource>()) {
      std::cout << "    No texture resource table present\n\n";
      co_return;
    }
    using namespace PrintUtils;
    SubSeparator("TEXTURE RESOURCES");
    auto& textures_table = pak.TexturesTable();
    size_t texture_count = textures_table.Size();
    Field("Texture Count", texture_count);
    if ((ctx.verbose || ctx.show_resource_data) && texture_count > 0) {
      std::cout << "    Texture entries:\n";
      for (size_t i = 0; i < (std::min)(texture_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<TextureResource>(
            pak, static_cast<uint32_t>(i));
          auto texture_resource
            = co_await asset_loader.LoadResourceAsync<TextureResource>(key);
          if (texture_resource) {
            std::cout << "      [" << i << "] Texture Resource:\n";
            Field(
              "Data Offset", ToHexString(texture_resource->GetDataOffset()), 8);
            Field("Data Size",
              std::to_string(texture_resource->GetData().size()) + " bytes", 8);
            Field("Width", std::to_string(texture_resource->GetWidth()), 8);
            Field("Height", std::to_string(texture_resource->GetHeight()), 8);
            Field("Depth", std::to_string(texture_resource->GetDepth()), 8);
            Field("Array Layers",
              std::to_string(texture_resource->GetArrayLayers()), 8);
            Field(
              "Mip Levels", std::to_string(texture_resource->GetMipCount()), 8);
            Field("Format", nostd::to_string(texture_resource->GetFormat()), 8);
            Field("Texture Type",
              nostd::to_string(texture_resource->GetTextureType()), 8);
            Field("Content Hash",
              ToHexString(texture_resource->GetContentHash()), 8);
            if (ctx.verbose) {
              PrintV4TexturePayloadSummary(texture_resource->GetData(), 8);
            }
            if (ctx.show_resource_data) {
              PrintResourceData(
                texture_resource->GetData(), "Texture", ctx.max_data_bytes);
            }
          } else {
            std::cout << "      [" << i
                      << "] Failed to load texture resource\n";
          }
        } catch (const std::exception& ex) {
          std::cout << "      [" << i
                    << "] Error loading texture: " << ex.what() << "\n";
        }
      }
      if (texture_count > 20) {
        std::cout << "      ... (" << (texture_count - 20)
                  << " more textures)\n";
      }
    }
    std::cout << "\n";
    co_return;
  }
};

class ScriptResourceTableDumper : public ResourceTableDumper {
public:
  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<> override
  {
    if (!ctx.show_resources) {
      co_return;
    }
    if (!pak.HasTableOf<ScriptResource>()) {
      std::cout << "    No script resource table present\n\n";
      co_return;
    }

    using namespace PrintUtils;
    SubSeparator("SCRIPT RESOURCES");
    auto& scripts_table = pak.ScriptsTable();
    const size_t script_count = scripts_table.Size();
    if (script_count > 0) {
      Field("Script Resource Count", script_count - 1);
      Field("Total Table Entries (inc. sentinel)", script_count);
    } else {
      Field("Script Resource Count", 0);
    }

    if ((ctx.verbose || ctx.show_resource_data) && script_count > 0) {
      std::cout << "    Script resource entries:\n";
      for (size_t i = 0; i < (std::min)(script_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<ScriptResource>(
            pak, static_cast<uint32_t>(i));
          auto script_resource
            = co_await asset_loader.LoadResourceAsync<ScriptResource>(key);
          if (script_resource) {
            std::cout << "      [" << i << "] Script Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(
              "Data Offset", ToHexString(script_resource->GetDataOffset()), 8);
            Field("Data Size",
              std::to_string(script_resource->GetDataSize()) + " bytes", 8);
            Field("Language",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetLanguage())),
              8);
            Field("Encoding",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetEncoding())),
              8);
            Field("Compression",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetCompression())),
              8);
            if (ctx.show_resource_data) {
              PrintResourceData(
                script_resource->GetData(), "Script", ctx.max_data_bytes);
            }
          } else {
            std::cout << "      [" << i << "] Failed to load script resource\n";
          }
        } catch (const std::exception& ex) {
          std::cout << "      [" << i << "] Error loading script: " << ex.what()
                    << "\n";
        }
      }
      if (script_count > 20) {
        std::cout << "      ... (" << (script_count - 20) << " more scripts)\n";
      }
    }
    std::cout << "\n";
    co_return;
  }
};

class PhysicsResourceTableDumper : public ResourceTableDumper {
public:
  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<> override
  {
    if (!ctx.show_resources) {
      co_return;
    }
    if (!pak.HasTableOf<PhysicsResource>()) {
      std::cout << "    No physics resource table present\n\n";
      co_return;
    }

    using namespace PrintUtils;
    SubSeparator("PHYSICS RESOURCES");
    auto& physics_table = pak.PhysicsTable();
    const size_t physics_count = physics_table.Size();
    if (physics_count > 0) {
      Field("Physics Resource Count", physics_count - 1);
      Field("Total Table Entries (inc. sentinel)", physics_count);
    } else {
      Field("Physics Resource Count", 0);
    }

    if ((ctx.verbose || ctx.show_resource_data) && physics_count > 0) {
      std::cout << "    Physics resource entries:\n";
      for (size_t i = 0; i < (std::min)(physics_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<PhysicsResource>(
            pak, static_cast<uint32_t>(i));
          auto physics_resource
            = co_await asset_loader.LoadResourceAsync<PhysicsResource>(key);
          if (physics_resource) {
            std::cout << "      [" << i << "] Physics Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(
              "Data Offset", ToHexString(physics_resource->GetDataOffset()), 8);
            Field("Data Size",
              std::to_string(physics_resource->GetDataSize()) + " bytes", 8);
            if (ctx.show_resource_data) {
              PrintResourceData(
                physics_resource->GetData(), "Physics", ctx.max_data_bytes);
            }
          } else {
            std::cout << "      [" << i
                      << "] Failed to load physics resource\n";
          }
        } catch (const std::exception& ex) {
          std::cout << "      [" << i
                    << "] Error loading physics resource: " << ex.what()
                    << "\n";
        }
      }
      if (physics_count > 20) {
        std::cout << "      ... (" << (physics_count - 20)
                  << " more physics resources)\n";
      }
    }
    std::cout << "\n";
    co_return;
  }
};

class ResourceTableDumperRegistry {
public:
  ResourceTableDumperRegistry()
  {
    Register("buffer", std::make_unique<BufferResourceTableDumper>());
    Register("texture", std::make_unique<TextureResourceTableDumper>());
    Register("script", std::make_unique<ScriptResourceTableDumper>());
    Register("physics", std::make_unique<PhysicsResourceTableDumper>());
  }

  const ResourceTableDumper& Get(const std::string& type) const
  {
    auto it = dumpers_.find(type);
    if (it != dumpers_.end()) {
      return *it->second;
    }
    return default_dumper_;
  }

  void Register(
    const std::string& type, std::unique_ptr<ResourceTableDumper> dumper)
  {
    dumpers_[type] = std::move(dumper);
  }

private:
  std::unordered_map<std::string, std::unique_ptr<ResourceTableDumper>>
    dumpers_;
  class DefaultResourceTableDumper : public ResourceTableDumper {
  public:
    auto DumpAsync(const PakFile&, DumpContext&, AssetLoader&) const
      -> oxygen::co::Co<> override
    {
      co_return;
    }
  } default_dumper_;
};

class ResourceTablesDumper {
public:
  ResourceTablesDumper(const ResourceTableDumperRegistry& registry)
    : registry_(registry)
  {
  }

  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<>
  {
    if (!ctx.show_resources) {
      co_return;
    }
    using namespace PrintUtils;
    Separator("RESOURCE TABLES");
    co_await registry_.Get("buffer").DumpAsync(pak, ctx, asset_loader);
    co_await registry_.Get("texture").DumpAsync(pak, ctx, asset_loader);
    co_await registry_.Get("script").DumpAsync(pak, ctx, asset_loader);
    co_await registry_.Get("physics").DumpAsync(pak, ctx, asset_loader);
    co_return;
  }

private:
  const ResourceTableDumperRegistry& registry_;
};

//=== PakFileDumper Class ===================================================//

auto PakFileDumper::DumpAsync(const PakFile& pak, AssetLoader& asset_loader)
  -> oxygen::co::Co<>
{
  try {
    constexpr auto kSupportedPakVersion = 7;
    if (pak.FormatVersion() != kSupportedPakVersion) {
      throw std::runtime_error(
        fmt::format("PakDump supports PAK v{} only; found version {}",
          kSupportedPakVersion, pak.FormatVersion()));
    }
    using namespace PrintUtils;
    Separator("PAK FILE ANALYSIS: " + ctx_.pak_path.filename().string());
    Field("File Path", ctx_.pak_path.string());
    Field("File Size",
      std::to_string(std::filesystem::file_size(ctx_.pak_path)) + " bytes");
    std::cout << "\n";
    PrintPakHeader(pak);
    PrintPakFooter(pak);
    ResourceTableDumperRegistry resource_registry;
    ResourceTablesDumper resource_tables_dumper(resource_registry);
    co_await resource_tables_dumper.DumpAsync(pak, ctx_, asset_loader);
    oxygen::content::pakdump::AssetDumperRegistry registry;
    oxygen::content::pakdump::AssetDirectoryDumper dir_dumper(registry);
    co_await dir_dumper.DumpAsync(pak, ctx_, asset_loader);
    Separator("ANALYSIS COMPLETE");
    co_return;
  } catch (const std::exception& ex) {
    std::cerr << "ERROR: PakDump failed: " << ex.what() << "\n";
    co_return;
  } catch (...) {
    std::cerr << "ERROR: PakDump failed: unknown exception\n";
    co_return;
  }
}

void PakFileDumper::PrintPakHeader(const PakFile& pak)
{
  if (!ctx_.show_header) {
    return;
  }
  using namespace PrintUtils;
  Separator("PAK HEADER");
  Field("Magic", "OXPAK (verified by successful load)");
  Field("Format Version", pak.FormatVersion());
  Field("Content Version", pak.ContentVersion());
  Field("GUID", oxygen::data::to_string(pak.Guid()));
  Field("Header Size", std::to_string(sizeof(PakHeader)) + " bytes");
  std::cout << "\n";
}

void PakFileDumper::PrintPakFooter(const PakFile& pak)
{
  if (!ctx_.show_footer) {
    return;
  }
  using namespace PrintUtils;
  Separator("PAK FOOTER");

  const auto footer = ReadPakFooter(ctx_.pak_path);
  if (!footer) {
    Field("Footer", "Failed to read from file");
    std::cout << "\n";
    return;
  }

  const auto& f = *footer;
  Field("Footer Size", std::to_string(sizeof(PakFooter)) + " bytes");
  Field("Footer Magic", FooterMagicOk(f) ? "OK" : "MISMATCH");

  Field("Directory Offset", ToHexString(f.directory_offset));
  Field("Directory Size", std::to_string(f.directory_size));
  Field("Asset Count (footer)", std::to_string(f.asset_count));
  std::cout << "\n";
  SubSeparator("RESOURCE REGIONS");
  PrintResourceRegion(
    "Texture Region", f.texture_region.offset, f.texture_region.size);
  PrintResourceRegion(
    "Buffer Region", f.buffer_region.offset, f.buffer_region.size);
  PrintResourceRegion(
    "Audio Region", f.audio_region.offset, f.audio_region.size);
  PrintResourceRegion(
    "Script Region", f.script_region.offset, f.script_region.size);
  PrintResourceRegion(
    "Physics Region", f.physics_region.offset, f.physics_region.size);
  std::cout << "\n";
  SubSeparator("RESOURCE TABLES");
  PrintResourceTable("Texture Table", f.texture_table.offset,
    f.texture_table.count, f.texture_table.entry_size);
  PrintResourceTable("Buffer Table", f.buffer_table.offset,
    f.buffer_table.count, f.buffer_table.entry_size);
  PrintResourceTable("Audio Table", f.audio_table.offset, f.audio_table.count,
    f.audio_table.entry_size);
  PrintResourceTable("Script Resource Table", f.script_resource_table.offset,
    f.script_resource_table.count, f.script_resource_table.entry_size);
  PrintResourceTable("Script Slot Table", f.script_slot_table.offset,
    f.script_slot_table.count, f.script_slot_table.entry_size);
  PrintResourceTable("Physics Resource Table", f.physics_resource_table.offset,
    f.physics_resource_table.count, f.physics_resource_table.entry_size);
  Field("Index-0 Sentinel",
    "buffer/scripts tables reserve index 0 as sentinel when present");

  Field("Browse Index Offset", ToHexString(f.browse_index_offset));
  Field("Browse Index Size", std::to_string(f.browse_index_size));
  Field("Browse Index Present", pak.HasBrowseIndex() ? "yes" : "no");
  if (pak.HasBrowseIndex()) {
    Field("Browse Index Entries", std::to_string(pak.BrowseIndex().size()));
  }

  Field("PAK CRC32", fmt::format("0x{:08x}", f.pak_crc32));

  if (ctx_.verbose && pak.HasBrowseIndex()) {
    std::cout << "\n";
    Separator("BROWSE INDEX");
    const auto entries = pak.BrowseIndex();
    constexpr size_t kMaxEntriesToPrint = 32;
    const auto count = (std::min)(entries.size(), kMaxEntriesToPrint);
    for (size_t i = 0; i < count; ++i) {
      const auto& e = entries[i];
      std::cout << "  [" << i << "]\n";
      Field("Virtual Path", e.virtual_path, 4);
      Field("Asset Key", to_string(e.asset_key), 4);
      std::cout << "\n";
    }
    if (entries.size() > kMaxEntriesToPrint) {
      std::cout << "  ... (" << (entries.size() - kMaxEntriesToPrint)
                << " more entries)\n\n";
    }
  }

  std::cout << "\n";
}
