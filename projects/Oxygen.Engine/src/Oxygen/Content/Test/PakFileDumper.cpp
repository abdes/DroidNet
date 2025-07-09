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

using namespace oxygen::content;
using namespace oxygen::data;
using namespace oxygen::data::pak;

//=== Configuration
//===========================================================//

struct DumpOptions {
  bool show_header = true;
  bool show_footer = true;
  bool show_directory = true;
  bool show_resources = true;
  bool show_resource_data = false; // For buffer/texture blob data
  bool show_asset_descriptors = false; // For asset descriptor hex dumps
  bool verbose = false;
  size_t max_data_bytes = 256; // Maximum bytes to dump for data
};

//=== Utility Functions ======================================================//

auto PrintSeparator(const std::string& title = "") -> void
{
  fmt::print("{}\n", std::string(78, '='));
  if (!title.empty()) {
    fmt::print("== {}\n", title);
    fmt::print("{}\n", std::string(78, '='));
  }
}

auto PrintSubSeparator(const std::string& title) -> void
{
  fmt::print("--- {} {}\n", title, std::string(70 - title.length(), '-'));
}

template <typename T>
auto PrintField(const std::string& name, const T& value, int indent = 4) -> void
{
  fmt::print("{:>{}}{:<20}{}\n", "", indent, name + ":", value);
}

auto PrintBytes(const std::string& name, const uint8_t* data, size_t size,
  int indent = 4) -> void
{
  std::string line = fmt::format("{:>{}}{}: ", "", indent, name);
  for (size_t i = 0; i < size; ++i) {
    if (i > 0 && i % 16 == 0) {
      fmt::print("{}\n", line);
      line = fmt::format("{:>{}}{}: ", "", indent, name);
    }
    line += fmt::format("{:02x} ", data[i]);
  }
  fmt::print("{}\n", line);
}

auto PrintHexDump(const uint8_t* data, size_t size, size_t max_bytes = 256)
  -> void
{
  size_t bytes_to_show = std::min(size, max_bytes);

  for (size_t i = 0; i < bytes_to_show; i += 16) {
    // Offset: decimal (right-aligned, width 4), then hex (8 digits,
    // zero-padded)
    std::string line = fmt::format("{:>4}: {:08x} ", i, i);

    // Hex bytes
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < bytes_to_show) {
        line += fmt::format("{:02x} ", data[i + j]);
      } else {
        line += "   ";
      }
    }

    line += " ";

    // ASCII representation
    for (size_t j = 0; j < 16 && i + j < bytes_to_show; ++j) {
      uint8_t c = data[i + j];
      line += (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
    }

    fmt::print("{}\n", line);
  }

  if (size > max_bytes) {
    std::cout << "    ... (" << (size - max_bytes) << " more bytes)\n";
  }

  // Reset fill character to default (space)
  std::cout << std::setfill(' ');
}

//=== Resource Data Access ===================================================//

/*!
 Helper function to print resource data (actual buffer/texture blob content).
 This is separate from asset descriptors - it reads the raw binary data
 that buffers and textures point to.
 */
auto PrintResourceData(const PakFile& pak, uint64_t data_offset,
  uint32_t data_size, const std::string& resource_type, size_t max_bytes = 256)
  -> void
{
  try {
    // Read data directly from the PAK file at the specified offset
    std::ifstream file(pak.FilePath(), std::ios::binary);
    if (!file.is_open()) {
      std::cout << "        Failed to open PAK file for data reading\n";
      return;
    }

    file.seekg(data_offset);
    if (file.fail()) {
      std::cout << "        Failed to seek to data offset 0x" << std::hex
                << data_offset << std::dec << "\n";
      return;
    }

    size_t bytes_to_read = std::min(static_cast<size_t>(data_size), max_bytes);
    std::vector<uint8_t> buffer(bytes_to_read);

    file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);
    if (file.fail()) {
      std::cout << "        Failed to read " << resource_type << " data\n";
      return;
    }

    std::cout << "        " << resource_type << " Data Preview ("
              << bytes_to_read << " of " << data_size << " bytes):\n";
    PrintHexDump(buffer.data(), bytes_to_read, max_bytes);

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

auto PrintAssetKey(const AssetKey& key, const DumpOptions& opts) -> void
{
  if (opts.verbose) {
    PrintField("GUID", to_string(key));
    PrintBytes(
      "Raw bytes", reinterpret_cast<const uint8_t*>(&key), sizeof(key));
  } else {
    PrintField("GUID", to_string(key));
  }
}

auto PrintPakHeader(const PakFile& pak, const DumpOptions& opts) -> void
{
  if (!opts.show_header) {
    return;
  }

  PrintSeparator("PAK HEADER");

  PrintField("Magic", "OXPAK (verified by successful load)");
  PrintField("Format Version", pak.FormatVersion());
  PrintField("Content Version", pak.ContentVersion());
  PrintField("Header Size", std::to_string(sizeof(PakHeader)) + " bytes");
  std::cout << "\n";
}

auto PrintResourceRegion(
  const std::string& name, uint64_t offset, uint64_t size) -> void
{
  auto msg = fmt::format("    {:<16}offset=0x{:08x}, size={} bytes{}\n",
    name + ":", offset, size, size == 0 ? " (empty)" : "");
  fmt::print("{}", msg);
}

auto PrintResourceTable(const std::string& name, uint64_t offset,
  uint32_t count, uint32_t entry_size) -> void
{
  auto msg = fmt::format(
    "    {:<16}offset=0x{:08x}, count={}, entry_size={} bytes{}\n", name + ":",
    offset, count, entry_size, count == 0 ? " (empty)" : "");
  fmt::print("{}", msg);
}

auto PrintPakFooter(const PakFile& pak, const DumpOptions& opts) -> void
{
  if (!opts.show_footer) {
    return;
  }

  PrintSeparator("PAK FOOTER");

  auto dir = pak.Directory();

  PrintField("Asset Count", dir.size());
  PrintField("Footer Size", std::to_string(sizeof(PakFooter)) + " bytes");
  std::cout << "\n";
}

template <typename T> auto ToHexString(T value) -> std::string
{
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

auto PrintBufferResourceTable(const PakFile& pak, const DumpOptions& opts,
  AssetLoader& asset_loader) -> void
{
  if (!opts.show_resources) {
    return;
  }

  if (!pak.HasTableOf<BufferResource>()) {
    std::cout << "    No buffer resource table present\n\n";
    return;
  }

  PrintSubSeparator("BUFFER RESOURCES");

  auto& buffers_table = pak.BuffersTable();
  size_t buffer_count = buffers_table.Size();

  PrintField("Buffer Count", buffer_count);

  if (opts.verbose && buffer_count > 0) {

    std::cout << "    Buffer entries:\n";
    for (size_t i = 0; i < std::min(buffer_count, static_cast<size_t>(20));
      ++i) {
      try {
        // Load buffer resource using AssetLoader in offline mode
        auto buffer_resource = asset_loader.LoadResource<BufferResource>(
          pak, static_cast<uint32_t>(i), true);
        if (buffer_resource) {
          std::cout << "      [" << i << "] Buffer Resource:\n";
          PrintField(
            "Data Offset", ToHexString(buffer_resource->GetDataOffset()), 8);
          PrintField("Data Size",
            std::to_string(buffer_resource->GetDataSize()) + " bytes", 8);
          PrintField("Element Stride",
            std::to_string(buffer_resource->GetElementStride()), 8);
          PrintField("Element Format",
            nostd::to_string(buffer_resource->GetElementFormat()), 8);
          PrintField("Usage Flags",
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
          PrintField("Buffer Type", buffer_type, 8);

          // Show buffer data if requested
          if (opts.show_resource_data) {
            PrintResourceData(pak, buffer_resource->GetDataOffset(),
              buffer_resource->GetDataSize(), "Buffer", opts.max_data_bytes);
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

auto PrintTextureResourceTable(const PakFile& pak, const DumpOptions& opts,
  AssetLoader& asset_loader) -> void
{
  if (!opts.show_resources) {
    return;
  }

  if (!pak.HasTableOf<TextureResource>()) {
    std::cout << "    No texture resource table present\n\n";
    return;
  }

  PrintSubSeparator("TEXTURE RESOURCES");

  auto& textures_table = pak.TexturesTable();
  size_t texture_count = textures_table.Size();

  PrintField("Texture Count", texture_count);

  if (opts.verbose && texture_count > 0) {

    std::cout << "    Texture entries:\n";
    for (size_t i = 0; i < std::min(texture_count, static_cast<size_t>(20));
      ++i) {
      try {
        // Load texture resource using AssetLoader in offline mode
        auto texture_resource = asset_loader.LoadResource<TextureResource>(
          pak, static_cast<uint32_t>(i), true);
        if (texture_resource) {
          std::cout << "      [" << i << "] Texture Resource:\n";
          PrintField(
            "Data Offset", ToHexString(texture_resource->GetDataOffset()), 8);
          PrintField("Data Size",
            std::to_string(texture_resource->GetDataSize()) + " bytes", 8);
          PrintField("Width", std::to_string(texture_resource->GetWidth()), 8);
          PrintField(
            "Height", std::to_string(texture_resource->GetHeight()), 8);
          PrintField("Depth", std::to_string(texture_resource->GetDepth()), 8);
          PrintField("Array Layers",
            std::to_string(texture_resource->GetArrayLayers()), 8);
          PrintField(
            "Mip Levels", std::to_string(texture_resource->GetMipCount()), 8);
          PrintField(
            "Format", nostd::to_string(texture_resource->GetFormat()), 8);
          PrintField("Texture Type",
            nostd::to_string(texture_resource->GetTextureType()), 8);

          // Show texture data if requested
          if (opts.show_resource_data) {
            PrintResourceData(pak, texture_resource->GetDataOffset(),
              texture_resource->GetDataSize(), "Texture", opts.max_data_bytes);
          }
        } else {
          std::cout << "      [" << i << "] Failed to load texture resource\n";
        }
      } catch (const std::exception& ex) {
        std::cout << "      [" << i << "] Error loading texture: " << ex.what()
                  << "\n";
      }
    }
    if (texture_count > 20) {
      std::cout << "      ... (" << (texture_count - 20) << " more textures)\n";
    }
  }
  std::cout << "\n";
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
  std::cout << "    --- Material Descriptor Fields ---\n";
  PrintField("Asset Type", static_cast<int>(h.asset_type), 8);
  PrintField("Name", std::string(h.name, strnlen(h.name, kMaxNameSize)), 8);
  PrintField("Version", static_cast<int>(h.version), 8);
  PrintField("Streaming Priority", static_cast<int>(h.streaming_priority), 8);
  PrintField("Content Hash", ToHexString(h.content_hash), 8);
  PrintField("Variant Flags", ToHexString(h.variant_flags), 8);
  // MaterialAssetDesc fields
  PrintField("Material Domain", static_cast<int>(mat->material_domain), 8);
  PrintField("Flags", ToHexString(mat->flags), 8);
  PrintField("Shader Stages", ToHexString(mat->shader_stages), 8);
  PrintField("Base Color",
    fmt::format("[{:.3f}, {:.3f}, {:.3f}, {:.3f}]", mat->base_color[0],
      mat->base_color[1], mat->base_color[2], mat->base_color[3]),
    8);
  PrintField("Normal Scale", mat->normal_scale, 8);
  PrintField("Metalness", mat->metalness, 8);
  PrintField("Roughness", mat->roughness, 8);
  PrintField("Ambient Occlusion", mat->ambient_occlusion, 8);
  PrintField("Base Color Texture", mat->base_color_texture, 8);
  PrintField("Normal Texture", mat->normal_texture, 8);
  PrintField("Metallic Texture", mat->metallic_texture, 8);
  PrintField("Roughness Texture", mat->roughness_texture, 8);
  PrintField("Ambient Occlusion Texture", mat->ambient_occlusion_texture, 8);
  // for (int i = 0; i < 8; ++i) {
  //   PrintField(
  //     fmt::format("Reserved Texture[{}]", i), mat->reserved_textures[i], 8);
  // }
  // // Reserved bytes (not printed individually)
  // PrintField("Reserved (68 bytes)", "...", 8);
  std::cout << "\n";
}

auto PrintShaderReference(const uint8_t* data, size_t size, size_t idx,
  size_t offset, const DumpOptions& opts) -> void
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
  PrintField("Unique ID", std::string(unique_id, strnlen(unique_id, 192)), 10);
  PrintField("Shader Hash", ToHexString(shader_hash), 10);
  // Only print hex dump if requested
  if (opts.show_asset_descriptors) {
    std::cout << "        Hex Dump (offset " << offset << ", size "
              << kShaderReferenceDescSize << "):\n";
    PrintHexDump(data, kShaderReferenceDescSize, kShaderReferenceDescSize);
  }
}

auto PrintMaterialShaderReferences(
  const uint8_t* data, size_t desc_size, const DumpOptions& opts) -> void
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
      data + base_offset, desc_size - base_offset, i, base_offset, opts);
    base_offset += kShaderReferenceDescSize;
  }
}

auto PrintAssetData(const PakFile& pak, const AssetDirectoryEntry& entry,
  const DumpOptions& opts) -> void
{
  try {
    auto reader = pak.CreateReader(entry);

    // Read the full descriptor (and shader refs if present)
    size_t bytes_to_read = static_cast<size_t>(entry.desc_size);
    auto data_result = reader.ReadBlob(bytes_to_read);

    if (data_result.has_value()) {
      const auto& data = data_result.value();
      // Print hex dump if requested
      if (opts.show_asset_descriptors) {
        std::cout << "    Asset Descriptor Preview (" << data.size()
                  << " bytes read):\n";
        PrintHexDump(reinterpret_cast<const uint8_t*>(data.data()),
          std::min(data.size(), opts.max_data_bytes), opts.max_data_bytes);
      }

      // If this is a material asset, print all descriptor fields
      if (entry.asset_type == 1 && data.size() >= kMaterialAssetDescSize) {
        const MaterialAssetDesc* mat
          = reinterpret_cast<const MaterialAssetDesc*>(data.data());
        PrintMaterialDescriptorFields(mat);
        PrintMaterialShaderReferences(
          reinterpret_cast<const uint8_t*>(data.data()), data.size(), opts);
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
  const PakFile& pak, const DumpOptions& opts) -> void
{
  std::cout << "Asset #" << idx << ":\n";
  PrintAssetKey(entry.asset_key, opts);
  std::cout << "    --- asset metadata ---\n";
  PrintField("Asset Type",
    std::string(GetAssetTypeName(entry.asset_type)) + " ("
      + std::to_string(entry.asset_type) + ")");
  PrintField("Entry Offset", ToHexString(entry.entry_offset));
  PrintField("Desc Offset", ToHexString(entry.desc_offset));
  PrintField("Desc Size", std::to_string(entry.desc_size) + " bytes");

  // Print asset descriptor if requested
  PrintAssetData(pak, entry, opts);
  std::cout << "\n";
}

auto PrintAssetDirectory(const PakFile& pak, const DumpOptions& opts) -> void
{
  if (!opts.show_directory) {
    return;
  }

  PrintSeparator("ASSET DIRECTORY");

  auto dir = pak.Directory();
  PrintField("Asset Count", dir.size());
  std::cout << "\n";

  for (size_t i = 0; i < dir.size(); ++i) {
    PrintAssetEntry(dir[i], i, pak, opts);
  }
}

auto ParseCommandLine(int argc, char* argv[]) -> DumpOptions
{
  DumpOptions opts;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--no-header") {
      opts.show_header = false;
    } else if (arg == "--no-footer") {
      opts.show_footer = false;
    } else if (arg == "--no-directory") {
      opts.show_directory = false;
    } else if (arg == "--no-resources") {
      opts.show_resources = false;
    } else if (arg == "--show-data") {
      opts.show_resource_data = true;
    } else if (arg == "--hex-dump-assets") {
      opts.show_asset_descriptors = true;
    } else if (arg == "--verbose") {
      opts.verbose = true;
    } else if (arg.starts_with("--max-data=")) {
      opts.max_data_bytes = std::stoul(arg.substr(11));
    }
  }

  return opts;
}

auto PrintUsage(const char* program_name) -> void
{
  std::cout << "Usage: " << program_name << " <pakfile> [options]\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --no-header        Don't show PAK header information\n";
  std::cout << "  --no-footer        Don't show PAK footer information\n";
  std::cout << "  --no-directory     Don't show asset directory\n";
  std::cout << "  --no-resources     Don't show resource table information\n";
  std::cout << "  --show-data        Show hex dump of resource data "
               "(buffers/textures)\n";
  std::cout << "  --hex-dump-assets  Show hex dump of asset descriptors\n";
  std::cout << "  --verbose          Show detailed information\n";
  std::cout << "  --max-data=N       Maximum bytes to show for data dumps "
               "(default: 256)\n";
  std::cout << "\nExamples:\n";
  std::cout << "  " << program_name << " game.pak\n";
  std::cout << "  " << program_name << " game.pak --verbose --show-data\n";
  std::cout << "  " << program_name << " game.pak --hex-dump-assets\n";
  std::cout << "  " << program_name
            << " game.pak --verbose --show-data --hex-dump-assets\n";
}

auto main(int argc, char* argv[]) -> int
{
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  // Configure logging
  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
  loguru::g_colorlogtostderr = true;
  loguru::init(argc, argv);
  loguru::set_thread_name("main");

  // Parse command line options
  DumpOptions opts = ParseCommandLine(argc, argv);

  std::filesystem::path pak_path(argv[1]);
  if (!std::filesystem::exists(pak_path)) {
    std::cerr << "File not found: " << pak_path << "\n";
    return 1;
  }

  try {
    PakFile pak(pak_path);

    // Create a single AssetLoader instance - built-in loaders are
    // auto-registered
    AssetLoader asset_loader;

    // Add the PAK file to the asset loader by path (for resource loading)
    asset_loader.AddPakFile(pak_path);

    PrintSeparator("PAK FILE ANALYSIS: " + pak_path.filename().string());
    PrintField("File Path", pak_path.string());
    PrintField("File Size",
      std::to_string(std::filesystem::file_size(pak_path)) + " bytes");
    std::cout << "\n";

    // Dump all sections based on options
    PrintPakHeader(pak, opts);
    PrintPakFooter(pak, opts);

    if (opts.show_resources) {
      PrintSeparator("RESOURCE TABLES");
      PrintBufferResourceTable(pak, opts, asset_loader);
      PrintTextureResourceTable(pak, opts, asset_loader);
    }

    PrintAssetDirectory(pak, opts);

    PrintSeparator("ANALYSIS COMPLETE");

  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 2;
  }

  return 0;
}
