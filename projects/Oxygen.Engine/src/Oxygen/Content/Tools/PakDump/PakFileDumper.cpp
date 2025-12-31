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
  constexpr std::string_view kFooterMagic = "OXPAKEND";
  return std::string_view(footer.footer_magic, sizeof(footer.footer_magic))
    == kFooterMagic;
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
    Field("Buffer Count", buffer_count);
    if (ctx.verbose && buffer_count > 0) {
      std::cout << "    Buffer entries:\n";
      for (size_t i = 0; i < (std::min)(buffer_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<BufferResource>(
            pak, static_cast<uint32_t>(i));
          auto buffer_resource
            = co_await asset_loader.LoadResourceAsync<BufferResource>(key);
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
    if (ctx.verbose && texture_count > 0) {
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
    // TODO: Add more resource types as needed
    co_return;
  }

private:
  const ResourceTableDumperRegistry& registry_;
};

//=== PakFileDumper Class ===================================================//

auto PakFileDumper::DumpAsync(const PakFile& pak, AssetLoader& asset_loader)
  -> oxygen::co::Co<>
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
  co_await resource_tables_dumper.DumpAsync(pak, ctx_, asset_loader);
  oxygen::content::pakdump::AssetDumperRegistry registry;
  oxygen::content::pakdump::AssetDirectoryDumper dir_dumper(registry);
  dir_dumper.Dump(pak, ctx_);
  Separator("ANALYSIS COMPLETE");
  co_return;
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
