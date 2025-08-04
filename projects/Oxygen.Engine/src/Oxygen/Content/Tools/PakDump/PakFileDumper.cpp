//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

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
      = std::min(static_cast<size_t>(data.size()), max_bytes);
    std::cout << "        " << resource_type << " Data Preview ("
              << bytes_to_read << " of " << data.size() << " bytes):\n";
    PrintUtils::HexDump(data.data(), bytes_to_read, max_bytes);

  } catch (const std::exception& ex) {
    std::cout << "        Failed to read " << resource_type
              << " data: " << ex.what() << "\n";
  }
}

//=== Asset Type Names =======================================================//

auto GetAssetTypeName(uint8_t asset_type) -> const char*
{
  return nostd::to_string(static_cast<AssetType>(asset_type));
}

//=== PAK Structure Dumping Functions ========================================//

auto PrintAssetKey(const AssetKey& key, DumpContext& ctx) -> void
{
  using namespace PrintUtils;
  if (ctx.verbose) {
    Field("GUID", to_string(key));
    Bytes("Raw bytes", reinterpret_cast<const uint8_t*>(&key), sizeof(key));
  } else {
    Field("GUID", to_string(key));
  }
}

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

/*!
 Helper function to print asset descriptor data (the metadata describing
 assets). This is separate from resource data - it reads the descriptor/metadata
 that describes how to interpret the asset.
 */
//=== Shader Reference Printing =============================================//

namespace {
constexpr size_t kMaterialAssetDescSize = 256;
constexpr size_t kShaderReferenceDescSize = 216;
}
// Prints all fields of a MaterialAssetDesc (including AssetHeader fields)
auto PrintMaterialDescriptorFields(const MaterialAssetDesc* mat) -> void
{
  const AssetHeader& h = mat->header;
  using namespace PrintUtils;
  std::cout << "    --- Material Descriptor Fields ---\n";
  Field("Asset Type", static_cast<int>(h.asset_type), 8);
  Field("Name", std::string(h.name, strnlen(h.name, kMaxNameSize)), 8);
  Field("Version", static_cast<int>(h.version), 8);
  Field("Streaming Priority", static_cast<int>(h.streaming_priority), 8);
  Field("Content Hash", ToHexString(h.content_hash), 8);
  Field("Variant Flags", ToHexString(h.variant_flags), 8);
  // MaterialAssetDesc fields
  Field("Material Domain", static_cast<int>(mat->material_domain), 8);
  Field("Flags", ToHexString(mat->flags), 8);
  Field("Shader Stages", ToHexString(mat->shader_stages), 8);
  Field("Base Color",
    fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat->base_color[0],
      mat->base_color[1], mat->base_color[2], mat->base_color[3]),
    8);
  Field("Normal Scale", mat->normal_scale, 8);
  Field("Metalness", mat->metalness, 8);
  Field("Roughness", mat->roughness, 8);
  Field("Ambient Occlusion", mat->ambient_occlusion, 8);
  Field("Base Color Texture", mat->base_color_texture, 8);
  Field("Normal Texture", mat->normal_texture, 8);
  Field("Metallic Texture", mat->metallic_texture, 8);
  Field("Roughness Texture", mat->roughness_texture, 8);
  Field("Ambient Occlusion Texture", mat->ambient_occlusion_texture, 8);
  std::cout << "\n";
}

auto PrintShaderReference(const uint8_t* data, size_t size, size_t idx,
  size_t offset, DumpContext& ctx) -> void
{
  if (size < kShaderReferenceDescSize) {
    std::cout << "      [" << idx
              << "] ShaderReferenceDesc: (insufficient data)\n";
    return;
  }
  // Parse fields
  const char* unique_id = reinterpret_cast<const char*>(data);
  uint64_t shader_hash = *reinterpret_cast<const uint64_t*>(data + 192);
  // Print fields
  std::cout << "      [" << idx << "] ShaderReferenceDesc:\n";
  using namespace PrintUtils;
  Field("Unique ID", std::string(unique_id, strnlen(unique_id, 192)), 10);
  Field("Shader Hash", ToHexString(shader_hash), 10);
  // Only print hex dump if requested
  if (ctx.show_asset_descriptors) {
    std::cout << "        Hex Dump (offset " << offset << ", size "
              << kShaderReferenceDescSize << "):\n";
    PrintUtils::HexDump(
      data, kShaderReferenceDescSize, kShaderReferenceDescSize);
  }
}

auto PrintMaterialShaderReferences(
  const uint8_t* data, size_t desc_size, DumpContext& ctx) -> void
{
  if (desc_size < kMaterialAssetDescSize)
    return;
  // Shader stages is at offset 100 (AssetHeader=95, +1 domain, +4 flags)
  uint32_t shader_stages = *reinterpret_cast<const uint32_t*>(data + 100);
  // Count set bits
  size_t num_refs = 0;
  for (uint32_t s = shader_stages; s; s >>= 1)
    num_refs += (s & 1);
  if (num_refs == 0)
    return;
  std::cout << "    Shader References (" << num_refs << "):\n";
  size_t base_offset = kMaterialAssetDescSize;
  for (size_t i = 0; i < num_refs; ++i) {
    if (base_offset + kShaderReferenceDescSize > desc_size)
      break;
    PrintShaderReference(
      data + base_offset, desc_size - base_offset, i, base_offset, ctx);
    base_offset += kShaderReferenceDescSize;
  }
}

auto PrintAssetData(const PakFile& pak, const AssetDirectoryEntry& entry,
  DumpContext& ctx) -> void
{
  try {
    auto reader = pak.CreateReader(entry);

    // Read the full descriptor (and shader refs if present)
    size_t bytes_to_read = static_cast<size_t>(entry.desc_size);
    auto data_result = reader.ReadBlob(bytes_to_read);

    if (data_result.has_value()) {
      const auto& data = data_result.value();
      // Print hex dump if requested
      if (ctx.show_asset_descriptors) {
        std::cout << "    Asset Descriptor Preview (" << data.size()
                  << " bytes read):\n";
        PrintUtils::HexDump(reinterpret_cast<const uint8_t*>(data.data()),
          std::min(data.size(), ctx.max_data_bytes), ctx.max_data_bytes);
      }

      // If this is a material asset, print all descriptor fields
      if (entry.asset_type == 1 && data.size() >= kMaterialAssetDescSize) {
        const MaterialAssetDesc* mat
          = reinterpret_cast<const MaterialAssetDesc*>(data.data());
        PrintMaterialDescriptorFields(mat);
        PrintMaterialShaderReferences(
          reinterpret_cast<const uint8_t*>(data.data()), data.size(), ctx);
      }
    } else {
      std::cout << "    Failed to read asset descriptor data\n";
    }

  } catch (const std::exception& ex) {
    std::cout << "    Failed to read asset descriptor data: " << ex.what()
              << "\n";
  }
}

auto PrintAssetEntry(const AssetDirectoryEntry& entry, size_t idx,
  const PakFile& pak, DumpContext& ctx) -> void
{
  std::cout << "Asset #" << idx << ":\n";
  PrintAssetKey(entry.asset_key, ctx);
  std::cout << "    --- asset metadata ---\n";
  PrintUtils::Field("Asset Type",
    std::string(GetAssetTypeName(entry.asset_type)) + " ("
      + std::to_string(entry.asset_type) + ")");
  PrintUtils::Field("Entry Offset", ToHexString(entry.entry_offset));
  PrintUtils::Field("Desc Offset", ToHexString(entry.desc_offset));
  PrintUtils::Field("Desc Size", std::to_string(entry.desc_size) + " bytes");

  // Print asset descriptor if requested
  PrintAssetData(pak, entry, ctx);
  std::cout << "\n";
}

auto PrintAssetDirectory(const PakFile& pak, DumpContext& ctx) -> void
{
  if (!ctx.show_directory) {
    return;
  }

  using namespace PrintUtils;
  Separator("ASSET DIRECTORY");

  auto dir = pak.Directory();
  Field("Asset Count", dir.size());
  std::cout << "\n";

  for (size_t i = 0; i < dir.size(); ++i) {
    PrintAssetEntry(dir[i], i, pak, ctx);
  }
}

//=== AssetDumper Interface and Registry ===================================//

class AssetDumper {
public:
  virtual ~AssetDumper() = default;
  virtual void Dump(const PakFile& pak, const AssetDirectoryEntry& entry,
    DumpContext& ctx, size_t idx) const
    = 0;
};

class MaterialAssetDumper : public AssetDumper {
public:
  void Dump(const PakFile& pak, const AssetDirectoryEntry& entry,
    DumpContext& ctx, size_t idx) const override
  {
    std::cout << "Asset #" << idx << ":\n";
    PrintAssetKey(entry.asset_key, ctx);
    std::cout << "    --- asset metadata ---\n";
    PrintUtils::Field("Asset Type",
      std::string(GetAssetTypeName(entry.asset_type)) + " ("
        + std::to_string(entry.asset_type) + ")");
    PrintUtils::Field("Entry Offset", ToHexString(entry.entry_offset));
    PrintUtils::Field("Desc Offset", ToHexString(entry.desc_offset));
    PrintUtils::Field("Desc Size", std::to_string(entry.desc_size) + " bytes");
    PrintAssetData(pak, entry, ctx);
    std::cout << "\n";
  }
};

// TODO: Add more AssetDumper implementations for other asset types (Geometry,
// Texture, etc.)

class DefaultAssetDumper : public AssetDumper {
public:
  void Dump(const PakFile& pak, const AssetDirectoryEntry& entry,
    DumpContext& ctx, size_t idx) const override
  {
    std::cout << "Asset #" << idx << ":\n";
    PrintAssetKey(entry.asset_key, ctx);
    std::cout << "    --- asset metadata ---\n";
    PrintUtils::Field("Asset Type",
      std::string(GetAssetTypeName(entry.asset_type)) + " ("
        + std::to_string(entry.asset_type) + ")");
    PrintUtils::Field("Entry Offset", ToHexString(entry.entry_offset));
    PrintUtils::Field("Desc Offset", ToHexString(entry.desc_offset));
    PrintUtils::Field("Desc Size", std::to_string(entry.desc_size) + " bytes");
    PrintAssetData(pak, entry, ctx);
    std::cout << "\n";
  }
};

class AssetDumperRegistry {
public:
  AssetDumperRegistry()
  {
    // Register known asset type dumpers
    Register(
      1, std::make_unique<MaterialAssetDumper>()); // 1 = MaterialAssetType
    // TODO: Register other asset type dumpers as needed
  }

  const AssetDumper& Get(uint8_t asset_type) const
  {
    auto it = dumpers_.find(asset_type);
    if (it != dumpers_.end()) {
      return *it->second;
    }
    return default_dumper_;
  }

  void Register(uint8_t asset_type, std::unique_ptr<AssetDumper> dumper)
  {
    dumpers_[asset_type] = std::move(dumper);
  }

private:
  std::unordered_map<uint8_t, std::unique_ptr<AssetDumper>> dumpers_;
  DefaultAssetDumper default_dumper_;
};

//=== AssetDirectoryDumper =================================================//

class AssetDirectoryDumper {
public:
  AssetDirectoryDumper(const AssetDumperRegistry& registry)
    : registry_(registry)
  {
  }

  void Dump(const PakFile& pak, DumpContext& ctx) const
  {
    if (!ctx.show_directory) {
      return;
    }
    using namespace PrintUtils;
    Separator("ASSET DIRECTORY");
    auto dir = pak.Directory();
    Field("Asset Count", dir.size());
    std::cout << "\n";
    for (size_t i = 0; i < dir.size(); ++i) {
      const auto& entry = dir[i];
      registry_.Get(entry.asset_type).Dump(pak, entry, ctx, i);
    }
  }

private:
  const AssetDumperRegistry& registry_;
};

//=== PakFileDumper Class ===================================================//

//=== ResourceTableDumper Interface and Registry ===========================//

class ResourceTableDumper {
public:
  virtual ~ResourceTableDumper() = default;
  virtual void Dump(
    const PakFile& pak, DumpContext& ctx, AssetLoader& asset_loader) const
    = 0;
};

class BufferResourceTableDumper : public ResourceTableDumper {
public:
  void Dump(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const override
  {
    if (!ctx.show_resources) {
      return;
    }
    if (!pak.HasTableOf<BufferResource>()) {
      std::cout << "    No buffer resource table present\n\n";
      return;
    }
    using namespace PrintUtils;
    SubSeparator("BUFFER RESOURCES");
    auto& buffers_table = pak.BuffersTable();
    size_t buffer_count = buffers_table.Size();
    Field("Buffer Count", buffer_count);
    if (ctx.verbose && buffer_count > 0) {
      std::cout << "    Buffer entries:\n";
      for (size_t i = 0; i < std::min(buffer_count, static_cast<size_t>(20));
        ++i) {
        try {
          auto buffer_resource = asset_loader.LoadResource<BufferResource>(
            pak, static_cast<uint32_t>(i), true);
          if (buffer_resource) {
            std::cout << "      [" << i << "] Buffer Resource:\n";
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
  }
};

class TextureResourceTableDumper : public ResourceTableDumper {
public:
  void Dump(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const override
  {
    if (!ctx.show_resources) {
      return;
    }
    if (!pak.HasTableOf<TextureResource>()) {
      std::cout << "    No texture resource table present\n\n";
      return;
    }
    using namespace PrintUtils;
    SubSeparator("TEXTURE RESOURCES");
    auto& textures_table = pak.TexturesTable();
    size_t texture_count = textures_table.Size();
    Field("Texture Count", texture_count);
    if (ctx.verbose && texture_count > 0) {
      std::cout << "    Texture entries:\n";
      for (size_t i = 0; i < std::min(texture_count, static_cast<size_t>(20));
        ++i) {
        try {
          auto texture_resource = asset_loader.LoadResource<TextureResource>(
            pak, static_cast<uint32_t>(i), true);
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
  }
};

class ResourceTableDumperRegistry {
public:
  ResourceTableDumperRegistry()
  {
    Register("buffer", std::make_unique<BufferResourceTableDumper>());
    Register("texture", std::make_unique<TextureResourceTableDumper>());
    // TODO: Register other resource table dumpers as needed
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
    void Dump(const PakFile&, DumpContext&, AssetLoader&) const override { }
  } default_dumper_;
};

class ResourceTablesDumper {
public:
  ResourceTablesDumper(const ResourceTableDumperRegistry& registry)
    : registry_(registry)
  {
  }

  void Dump(
    const PakFile& pak, DumpContext& ctx, AssetLoader& asset_loader) const
  {
    if (!ctx.show_resources) {
      return;
    }
    using namespace PrintUtils;
    Separator("RESOURCE TABLES");
    registry_.Get("buffer").Dump(pak, ctx, asset_loader);
    registry_.Get("texture").Dump(pak, ctx, asset_loader);
    // TODO: Add more resource types as needed
  }

private:
  const ResourceTableDumperRegistry& registry_;
};

//=== PakFileDumper Class ===================================================//

void PakFileDumper::Dump(const PakFile& pak, AssetLoader& asset_loader)
{
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
  resource_tables_dumper.Dump(pak, ctx_, asset_loader);
  AssetDumperRegistry registry;
  AssetDirectoryDumper dir_dumper(registry);
  dir_dumper.Dump(pak, ctx_);
  Separator("ANALYSIS COMPLETE");
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
  auto dir = pak.Directory();
  Field("Asset Count", dir.size());
  Field("Footer Size", std::to_string(sizeof(PakFooter)) + " bytes");
  std::cout << "\n";
}
