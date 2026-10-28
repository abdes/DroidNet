//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
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
#include <unordered_map>
#include <utility>
#include <vector>

#include "AssetDumpers.h"
#include "DumpContext.h"
#include "PakFileDumper.h"
#include "PrintUtils.h"
#include <fmt/base.h>
#include <fmt/format.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>

using PrintUtils::Field;
using PrintUtils::Separator;
using PrintUtils::SubSeparator;

using oxygen::content::AssetLoader;
using oxygen::content::PakFile;
using oxygen::data::BufferResource;
using oxygen::data::PhysicsResource;
using oxygen::data::ScriptResource;
using oxygen::data::TextureResource;
using oxygen::data::to_string;
using oxygen::data::pak::core::kPakFooterMagic;
using oxygen::data::pak::core::PakFooter;
using oxygen::data::pak::core::PakHeader;
using oxygen::data::pak::core::ResourceIndexT;
using oxygen::data::pak::render::kTexturePayloadMagic;
using oxygen::data::pak::render::SubresourceLayout;
using oxygen::data::pak::render::TexturePayloadHeader;

//=== Resource Data Access ===================================================//

/*!
 Helper function to print resource data (actual buffer/texture blob content).
 This is separate from asset descriptors - it reads the raw binary data
 that buffers and textures point to.
 */
auto PrintResourceData(std::ostream& out, std::span<const uint8_t> data,
  const std::string& resource_type, size_t max_bytes = 256) -> void
{
  try {
    size_t bytes_to_read
      = (std::min)(static_cast<size_t>(data.size()), max_bytes);
    out << "        " << resource_type << " Data Preview (" << bytes_to_read
        << " of " << data.size() << " bytes):\n";
    PrintUtils::HexDump(out, data.data(), bytes_to_read, max_bytes);

  } catch (const std::exception& ex) {
    out << "        Failed to read " << resource_type << " data: " << ex.what()
        << "\n";
  }
}

//=== PAK Structure Dumping Functions ========================================//

auto PrintResourceRegion(std::ostream& out, const std::string& name,
  uint64_t offset, uint64_t size) -> void
{
  out << fmt::format("    {:<16}offset=0x{:08x}, size={} bytes{}\n", name + ":",
    offset, size, size == 0 ? " (empty)" : "");
}

auto PrintResourceTable(std::ostream& out, const std::string& name,
  uint64_t offset, uint32_t count, uint32_t entry_size) -> void
{
  out << fmt::format(
    "    {:<16}offset=0x{:08x}, count={}, entry_size={} bytes{}\n", name + ":",
    offset, count, entry_size, count == 0 ? " (empty)" : "");
}

template <typename T> auto ToHexString(T value) -> std::string
{
  std::ostringstream oss;
  oss << "0x" << std::hex << value;
  return oss.str();
}

auto ToHexString(const oxygen::data::pak::core::ContentHashDigest& value)
  -> std::string
{
  std::ostringstream oss;
  oss << "0x";
  for (const auto byte : value) {
    oss << std::setw(2) << std::setfill('0') << std::hex
        << static_cast<unsigned>(byte);
  }
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
  const std::streamoff file_size = file.tellg();
  if (std::cmp_less(file_size, sizeof(PakFooter))) {
    return std::nullopt;
  }

  file.seekg(file_size - static_cast<std::streamoff>(sizeof(PakFooter)));
  std::array<char, sizeof(PakFooter)> raw {};
  file.read(raw.data(), static_cast<std::streamsize>(raw.size()));
  if (!file) {
    return std::nullopt;
  }
  PakFooter footer {};
  std::memcpy(&footer, raw.data(), sizeof(footer));
  return footer;
}

auto FooterMagicOk(const PakFooter& footer) -> bool
{
  return std::ranges::equal(std::span { footer.footer_magic }, kPakFooterMagic);
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

auto PrintV4TexturePayloadSummary(
  std::ostream& out, std::span<const uint8_t> payload, int indent) -> void
{
  if (payload.size() < sizeof(TexturePayloadHeader)) {
    Field(out, "Texture Payload", "Too small to contain v4 header", indent);
    return;
  }

  TexturePayloadHeader header {};
  std::memcpy(&header, payload.data(), sizeof(header));
  if (header.magic != kTexturePayloadMagic) {
    Field(out, "Texture Payload", "Missing v4 magic (expected 'OTX1')", indent);
    return;
  }

  Field(out, "Payload Magic", "OTX1", indent);
  Field(out, "Packing Policy",
    fmt::format("{} ({})", static_cast<int>(header.packing_policy),
      TexturePackingPolicyName(header.packing_policy)),
    indent);
  Field(out, "Flags", fmt::format("0x{:02x}", header.flags), indent);
  Field(out, "Subresources", std::to_string(header.subresource_count), indent);
  Field(out, "Total Payload Size", std::to_string(header.total_payload_size),
    indent);
  Field(
    out, "Layouts Offset", ToHexString(header.layouts_offset_bytes), indent);
  Field(out, "Data Offset", ToHexString(header.data_offset_bytes), indent);
  Field(out, "Payload Content Hash", ToHexString(header.content_hash), indent);

  if (header.total_payload_size != payload.size()) {
    Field(out, "Payload Size Check",
      fmt::format("mismatch (header={} actual={})", header.total_payload_size,
        payload.size()),
      indent);
  }

  const auto layouts_offset = static_cast<size_t>(header.layouts_offset_bytes);
  const auto data_offset = static_cast<size_t>(header.data_offset_bytes);
  const auto layout_count = static_cast<size_t>(header.subresource_count);
  const auto layouts_bytes = layout_count * sizeof(SubresourceLayout);

  if ((layouts_offset + layouts_bytes) > payload.size()) {
    Field(out, "Layouts", "Out of bounds", indent);
    return;
  }
  if (data_offset > payload.size()) {
    Field(out, "Data Section", "Out of bounds", indent);
    return;
  }

  constexpr size_t kMaxLayoutsToPrint = 16;
  const auto count = (std::min)(layout_count, kMaxLayoutsToPrint);
  if (count == 0) {
    return;
  }

  out << std::string(static_cast<size_t>(indent), ' ')
      << "Subresource Layouts (" << count
      << (layout_count > count ? " shown" : "") << "):\n";
  for (size_t i = 0; i < count; ++i) {
    SubresourceLayout layout {};
    const auto offset = layouts_offset + (i * sizeof(SubresourceLayout));
    std::memcpy(
      &layout, payload.subspan(offset, sizeof(layout)).data(), sizeof(layout));
    const auto abs_data_offset
      = data_offset + static_cast<size_t>(layout.offset_bytes);
    const auto end = abs_data_offset + static_cast<size_t>(layout.size_bytes);
    const auto within_bounds = end <= payload.size();

    out << std::string(static_cast<size_t>(indent), ' ') << "  [" << i
        << "] offset=" << ToHexString(layout.offset_bytes)
        << " row_pitch=" << layout.row_pitch_bytes
        << " size=" << layout.size_bytes << (within_bounds ? "" : " (oob)")
        << "\n";
  }
  if (layout_count > kMaxLayoutsToPrint) {
    out << std::string(static_cast<size_t>(indent), ' ') << "  ... ("
        << (layout_count - kMaxLayoutsToPrint) << " more)\n";
  }
}

} // namespace

//=== ResourceTableDumper Interface and Registry ===========================//

class ResourceTableDumper {
public:
  ResourceTableDumper() = default;
  virtual ~ResourceTableDumper() = default;

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableDumper)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableDumper)

  virtual auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<> = 0;
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
      ctx.Out() << "    No buffer resource table present\n\n";
      co_return;
    }
    SubSeparator(ctx.Out(), "BUFFER RESOURCES");
    auto& buffers_table = pak.BuffersTable();
    size_t buffer_count = buffers_table.Size();
    if (buffer_count > 0) {
      Field(ctx.Out(), "Buffer Count", buffer_count - 1);
      Field(ctx.Out(), "Total Table Entries (inc. sentinel)", buffer_count);
    } else {
      Field(ctx.Out(), "Buffer Count", 0);
    }
    if ((ctx.verbose || ctx.show_resource_data) && buffer_count > 0) {
      ctx.Out() << "    Buffer entries:\n";
      for (size_t i = 0; i < (std::min)(buffer_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<BufferResource>(
            pak, ResourceIndexT { static_cast<uint32_t>(i) });
          auto buffer_resource
            = co_await asset_loader.LoadResourceAsync<BufferResource>(key);
          if (buffer_resource) {
            ctx.Out() << "      [" << i << "] Buffer Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(ctx.Out(), "Data Offset",
              ToHexString(buffer_resource->GetDataOffset()), 8);
            Field(ctx.Out(), "Data Size",
              std::to_string(buffer_resource->GetDataSize()) + " bytes", 8);
            Field(ctx.Out(), "Element Stride",
              std::to_string(buffer_resource->GetElementStride()), 8);
            Field(ctx.Out(), "Element Format",
              std::to_string(
                static_cast<uint32_t>(buffer_resource->GetElementFormat())),
              8);
            Field(ctx.Out(), "Usage Flags",
              std::to_string(
                static_cast<uint32_t>(buffer_resource->GetUsageFlags())),
              8);
            Field(ctx.Out(), "Content Hash",
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
            Field(ctx.Out(), "Buffer Type", buffer_type, 8);
            if (ctx.show_resource_data) {
              PrintResourceData(ctx.Out(), buffer_resource->GetData(), "Buffer",
                ctx.max_data_bytes);
            }
          } else {
            ctx.Out() << "      [" << i << "] Failed to load buffer resource\n";
          }
        } catch (const std::exception& ex) {
          ctx.Out() << "      [" << i << "] Error loading buffer: " << ex.what()
                    << "\n";
        }
      }
      if (buffer_count > 20) {
        ctx.Out() << "      ... (" << (buffer_count - 20) << " more buffers)\n";
      }
    }
    ctx.Out() << "\n";
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
      ctx.Out() << "    No texture resource table present\n\n";
      co_return;
    }
    SubSeparator(ctx.Out(), "TEXTURE RESOURCES");
    auto& textures_table = pak.TexturesTable();
    size_t texture_count = textures_table.Size();
    Field(ctx.Out(), "Texture Count", texture_count);
    if ((ctx.verbose || ctx.show_resource_data) && texture_count > 0) {
      ctx.Out() << "    Texture entries:\n";
      for (size_t i = 0; i < (std::min)(texture_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<TextureResource>(
            pak, ResourceIndexT { static_cast<uint32_t>(i) });
          auto texture_resource
            = co_await asset_loader.LoadResourceAsync<TextureResource>(key);
          if (texture_resource) {
            ctx.Out() << "      [" << i << "] Texture Resource:\n";
            Field(ctx.Out(), "Data Offset",
              ToHexString(texture_resource->GetDataOffset()), 8);
            Field(ctx.Out(), "Data Size",
              std::to_string(texture_resource->GetData().size()) + " bytes", 8);
            Field(ctx.Out(), "Width",
              std::to_string(texture_resource->GetWidth()), 8);
            Field(ctx.Out(), "Height",
              std::to_string(texture_resource->GetHeight()), 8);
            Field(ctx.Out(), "Depth",
              std::to_string(texture_resource->GetDepth()), 8);
            Field(ctx.Out(), "Array Layers",
              std::to_string(texture_resource->GetArrayLayers()), 8);
            Field(ctx.Out(), "Mip Levels",
              std::to_string(texture_resource->GetMipCount()), 8);
            Field(ctx.Out(), "Format",
              std::to_string(
                static_cast<uint32_t>(texture_resource->GetFormat())),
              8);
            Field(ctx.Out(), "Texture Type",
              std::to_string(
                static_cast<uint32_t>(texture_resource->GetTextureType())),
              8);
            Field(ctx.Out(), "Content Hash",
              ToHexString(texture_resource->GetContentHash()), 8);
            if (ctx.verbose) {
              PrintV4TexturePayloadSummary(
                ctx.Out(), texture_resource->GetPayload(), 8);
            }
            if (ctx.show_resource_data) {
              PrintResourceData(ctx.Out(), texture_resource->GetData(),
                "Texture", ctx.max_data_bytes);
            }
          } else {
            ctx.Out() << "      [" << i
                      << "] Failed to load texture resource\n";
          }
        } catch (const std::exception& ex) {
          ctx.Out() << "      [" << i
                    << "] Error loading texture: " << ex.what() << "\n";
        }
      }
      if (texture_count > 20) {
        ctx.Out() << "      ... (" << (texture_count - 20)
                  << " more textures)\n";
      }
    }
    ctx.Out() << "\n";
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
      ctx.Out() << "    No script resource table present\n\n";
      co_return;
    }

    SubSeparator(ctx.Out(), "SCRIPT RESOURCES");
    auto& scripts_table = pak.ScriptsTable();
    const size_t script_count = scripts_table.Size();
    if (script_count > 0) {
      Field(ctx.Out(), "Script Resource Count", script_count - 1);
      Field(ctx.Out(), "Total Table Entries (inc. sentinel)", script_count);
    } else {
      Field(ctx.Out(), "Script Resource Count", 0);
    }

    if ((ctx.verbose || ctx.show_resource_data) && script_count > 0) {
      ctx.Out() << "    Script resource entries:\n";
      for (size_t i = 0; i < (std::min)(script_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<ScriptResource>(
            pak, ResourceIndexT { static_cast<uint32_t>(i) });
          auto script_resource
            = co_await asset_loader.LoadResourceAsync<ScriptResource>(key);
          if (script_resource) {
            ctx.Out() << "      [" << i << "] Script Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(ctx.Out(), "Data Offset",
              ToHexString(script_resource->GetDataOffset()), 8);
            Field(ctx.Out(), "Data Size",
              std::to_string(script_resource->GetDataSize()) + " bytes", 8);
            Field(ctx.Out(), "Language",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetLanguage())),
              8);
            Field(ctx.Out(), "Encoding",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetEncoding())),
              8);
            Field(ctx.Out(), "Compression",
              std::to_string(
                static_cast<uint32_t>(script_resource->GetCompression())),
              8);
            if (ctx.show_resource_data) {
              PrintResourceData(ctx.Out(), script_resource->GetData(), "Script",
                ctx.max_data_bytes);
            }
          } else {
            ctx.Out() << "      [" << i << "] Failed to load script resource\n";
          }
        } catch (const std::exception& ex) {
          ctx.Out() << "      [" << i << "] Error loading script: " << ex.what()
                    << "\n";
        }
      }
      if (script_count > 20) {
        ctx.Out() << "      ... (" << (script_count - 20) << " more scripts)\n";
      }
    }
    ctx.Out() << "\n";
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
      ctx.Out() << "    No physics resource table present\n\n";
      co_return;
    }

    SubSeparator(ctx.Out(), "PHYSICS RESOURCES");
    auto& physics_table = pak.PhysicsTable();
    const size_t physics_count = physics_table.Size();
    if (physics_count > 0) {
      Field(ctx.Out(), "Physics Resource Count", physics_count - 1);
      Field(ctx.Out(), "Total Table Entries (inc. sentinel)", physics_count);
    } else {
      Field(ctx.Out(), "Physics Resource Count", 0);
    }

    if ((ctx.verbose || ctx.show_resource_data) && physics_count > 0) {
      ctx.Out() << "    Physics resource entries:\n";
      for (size_t i = 0; i < (std::min)(physics_count, static_cast<size_t>(20));
        ++i) {
        try {
          const auto key = asset_loader.MakeResourceKey<PhysicsResource>(
            pak, ResourceIndexT { static_cast<uint32_t>(i) });
          auto physics_resource
            = co_await asset_loader.LoadResourceAsync<PhysicsResource>(key);
          if (physics_resource) {
            ctx.Out() << "      [" << i << "] Physics Resource"
                      << (i == 0 ? " (sentinel/reserved)" : "") << ":\n";
            Field(ctx.Out(), "Data Offset",
              ToHexString(physics_resource->GetDataOffset()), 8);
            Field(ctx.Out(), "Data Size",
              std::to_string(physics_resource->GetDataSize()) + " bytes", 8);
            if (ctx.show_resource_data) {
              PrintResourceData(ctx.Out(), physics_resource->GetData(),
                "Physics", ctx.max_data_bytes);
            }
          } else {
            ctx.Out() << "      [" << i
                      << "] Failed to load physics resource\n";
          }
        } catch (const std::exception& ex) {
          ctx.Out() << "      [" << i
                    << "] Error loading physics resource: " << ex.what()
                    << "\n";
        }
      }
      if (physics_count > 20) {
        ctx.Out() << "      ... (" << (physics_count - 20)
                  << " more physics resources)\n";
      }
    }
    ctx.Out() << "\n";
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

  [[nodiscard]] auto Get(const std::string& type) const
    -> const ResourceTableDumper&
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
    auto DumpAsync(const PakFile& /*pak*/, DumpContext& /*ctx*/,
      AssetLoader& /*loader*/) const -> oxygen::co::Co<> override
    {
      co_return;
    }
  } default_dumper_;
};

class ResourceTablesDumper {
public:
  explicit ResourceTablesDumper(const ResourceTableDumperRegistry& registry)
    : registry_(&registry)
  {
  }

  auto DumpAsync(const PakFile& pak, DumpContext& ctx,
    AssetLoader& asset_loader) const -> oxygen::co::Co<>
  {
    if (!ctx.show_resources) {
      co_return;
    }
    Separator(ctx.Out(), "RESOURCE TABLES");
    co_await registry_->Get("buffer").DumpAsync(pak, ctx, asset_loader);
    co_await registry_->Get("texture").DumpAsync(pak, ctx, asset_loader);
    co_await registry_->Get("script").DumpAsync(pak, ctx, asset_loader);
    co_await registry_->Get("physics").DumpAsync(pak, ctx, asset_loader);
    co_return;
  }

private:
  oxygen::observer_ptr<const ResourceTableDumperRegistry> registry_;
};

//=== PakFileDumper Class ===================================================//

auto PakFileDumper::DumpAsync(const PakFile& pak, AssetLoader& asset_loader)
  -> oxygen::co::Co<bool>
{
  try {
    for (const auto& entry : pak.Directory()) {
      const auto expected
        = oxygen::data::pak::CurrentAssetVersion(entry.asset_type);
      if (!expected
        || entry.desc_size < sizeof(oxygen::data::pak::core::AssetHeader)) {
        throw std::runtime_error("Unsupported or truncated asset descriptor: "
          + to_string(entry.asset_key));
      }
      auto reader = pak.CreateReader(entry);
      const auto header = reader.Read<oxygen::data::pak::core::AssetHeader>();
      if (!header
        || header->asset_type != static_cast<uint8_t>(entry.asset_type)
        || header->version != *expected) {
        throw std::runtime_error(fmt::format(
          "Asset {} requires the current {} descriptor version {}; re-cook the "
          "content",
          to_string(entry.asset_key), to_string(entry.asset_type), *expected));
      }
    }
    Separator(
      ctx_.Out(), "PAK FILE ANALYSIS: " + ctx_.pak_path.filename().string());
    Field(ctx_.Out(), "File Path", ctx_.pak_path.string());
    Field(ctx_.Out(), "File Size",
      std::to_string(std::filesystem::file_size(ctx_.pak_path)) + " bytes");
    ctx_.Out() << "\n";
    PrintPakHeader(pak);
    PrintPakFooter(pak);
    ResourceTableDumperRegistry resource_registry;
    ResourceTablesDumper resource_tables_dumper(resource_registry);
    co_await resource_tables_dumper.DumpAsync(pak, ctx_, asset_loader);
    oxygen::content::pakdump::AssetDumperRegistry registry;
    oxygen::content::pakdump::AssetDirectoryDumper dir_dumper(registry);
    co_await dir_dumper.DumpAsync(pak, ctx_, asset_loader);
    Separator(ctx_.Out(), "ANALYSIS COMPLETE");
    co_return true;
  } catch (const std::exception& ex) {
    ctx_.Err() << "ERROR: PakDump failed: " << ex.what() << "\n";
    co_return false;
  } catch (...) {
    ctx_.Err() << "ERROR: PakDump failed: unknown exception\n";
    co_return false;
  }
}

void PakFileDumper::PrintPakHeader(const PakFile& pak)
{
  if (!ctx_.show_header) {
    return;
  }
  Separator(ctx_.Out(), "PAK HEADER");
  Field(ctx_.Out(), "Magic", "OXPAK (verified by successful load)");
  Field(ctx_.Out(), "Format Version", pak.FormatVersion());
  Field(ctx_.Out(), "Content Version", pak.ContentVersion());
  Field(ctx_.Out(), "GUID", to_string(pak.Guid()));
  Field(
    ctx_.Out(), "Header Size", std::to_string(sizeof(PakHeader)) + " bytes");
  ctx_.Out() << "\n";
}

void PakFileDumper::PrintPakFooter(const PakFile& pak)
{
  if (!ctx_.show_footer) {
    return;
  }
  Separator(ctx_.Out(), "PAK FOOTER");

  const auto footer = ReadPakFooter(ctx_.pak_path);
  if (!footer) {
    Field(ctx_.Out(), "Footer", "Failed to read from file");
    ctx_.Out() << "\n";
    return;
  }

  const auto& f = *footer;
  Field(
    ctx_.Out(), "Footer Size", std::to_string(sizeof(PakFooter)) + " bytes");
  Field(ctx_.Out(), "Footer Magic", FooterMagicOk(f) ? "OK" : "MISMATCH");

  Field(ctx_.Out(), "Directory Offset", ToHexString(f.directory_offset));
  Field(ctx_.Out(), "Directory Size", std::to_string(f.directory_size));
  Field(ctx_.Out(), "Asset Count (footer)", std::to_string(f.asset_count));
  ctx_.Out() << "\n";
  SubSeparator(ctx_.Out(), "RESOURCE REGIONS");
  PrintResourceRegion(ctx_.Out(), "Texture Region", f.texture_region.offset,
    f.texture_region.size);
  PrintResourceRegion(
    ctx_.Out(), "Buffer Region", f.buffer_region.offset, f.buffer_region.size);
  PrintResourceRegion(
    ctx_.Out(), "Audio Region", f.audio_region.offset, f.audio_region.size);
  PrintResourceRegion(
    ctx_.Out(), "Script Region", f.script_region.offset, f.script_region.size);
  PrintResourceRegion(ctx_.Out(), "Physics Region", f.physics_region.offset,
    f.physics_region.size);
  ctx_.Out() << "\n";
  SubSeparator(ctx_.Out(), "RESOURCE TABLES");
  PrintResourceTable(ctx_.Out(), "Texture Table", f.texture_table.offset,
    f.texture_table.count, f.texture_table.entry_size);
  PrintResourceTable(ctx_.Out(), "Buffer Table", f.buffer_table.offset,
    f.buffer_table.count, f.buffer_table.entry_size);
  PrintResourceTable(ctx_.Out(), "Audio Table", f.audio_table.offset,
    f.audio_table.count, f.audio_table.entry_size);
  PrintResourceTable(ctx_.Out(), "Script Resource Table",
    f.script_resource_table.offset, f.script_resource_table.count,
    f.script_resource_table.entry_size);
  PrintResourceTable(ctx_.Out(), "Physics Resource Table",
    f.physics_resource_table.offset, f.physics_resource_table.count,
    f.physics_resource_table.entry_size);
  Field(ctx_.Out(), "Index-0 Sentinel",
    "buffer/scripts tables reserve index 0 as sentinel when present");

  Field(ctx_.Out(), "Browse Index Offset", ToHexString(f.browse_index_offset));
  Field(ctx_.Out(), "Browse Index Size", std::to_string(f.browse_index_size));
  Field(
    ctx_.Out(), "Browse Index Present", pak.HasBrowseIndex() ? "yes" : "no");
  if (pak.HasBrowseIndex()) {
    Field(ctx_.Out(), "Browse Index Entries",
      std::to_string(pak.BrowseIndex().size()));
    Field(ctx_.Out(), "Browse Index Textures",
      std::to_string(pak.BrowseTextures().size()));
  }

  Field(ctx_.Out(), "Catalog Offset", ToHexString(f.catalog_offset));
  Field(ctx_.Out(), "Catalog Size", std::to_string(f.catalog_size));
  const auto& catalog = pak.Catalog();
  Field(ctx_.Out(), "Catalog Assets", std::to_string(catalog.entries.size()));
  Field(ctx_.Out(), "Deleted Assets", std::to_string(catalog.deleted.size()));
  Field(
    ctx_.Out(), "Required Base Layers", std::to_string(catalog.bases.size()));
  for (const auto& base : catalog.bases) {
    Field(
      ctx_.Out(), "Base Source", oxygen::data::to_string(base.source_key), 4);
    Field(ctx_.Out(), "Base Content Version",
      std::to_string(base.content_version), 4);
  }
  Field(
    ctx_.Out(), "Dependencies", std::to_string(catalog.dependencies.size()));
  for (const auto& dependency : catalog.dependencies) {
    Field(ctx_.Out(), "Dependency Source",
      oxygen::data::to_string(dependency.source_key), 4);
    Field(ctx_.Out(), "Dependency Content Version",
      std::to_string(dependency.content_version), 4);
  }

  Field(ctx_.Out(), "PAK CRC32", fmt::format("0x{:08x}", f.pak_crc32));

  if (ctx_.verbose && pak.HasBrowseIndex()) {
    ctx_.Out() << "\n";
    Separator(ctx_.Out(), "BROWSE INDEX");
    const auto entries = pak.BrowseIndex();
    constexpr size_t kMaxEntriesToPrint = 32;
    const auto count = (std::min)(entries.size(), kMaxEntriesToPrint);
    for (size_t i = 0; i < count; ++i) {
      const auto& e = entries[i];
      ctx_.Out() << "  [" << i << "]\n";
      Field(ctx_.Out(), "Virtual Path", e.virtual_path, 4);
      Field(ctx_.Out(), "Asset Key", to_string(e.asset_key), 4);
      ctx_.Out() << "\n";
    }
    if (entries.size() > kMaxEntriesToPrint) {
      ctx_.Out() << "  ... (" << (entries.size() - kMaxEntriesToPrint)
                 << " more entries)\n\n";
    }
    const auto textures = pak.BrowseTextures();
    const auto texture_count = (std::min)(textures.size(), kMaxEntriesToPrint);
    for (size_t i = 0; i < texture_count; ++i) {
      const auto& e = textures[i];
      ctx_.Out() << "  [texture " << i << "]\n";
      Field(ctx_.Out(), "Virtual Path", e.virtual_path, 4);
      Field(
        ctx_.Out(), "Texture Index", std::to_string(e.texture_index.get()), 4);
      ctx_.Out() << "\n";
    }
    if (textures.size() > kMaxEntriesToPrint) {
      ctx_.Out() << "  ... (" << (textures.size() - kMaxEntriesToPrint)
                 << " more textures)\n\n";
    }
  }

  ctx_.Out() << "\n";
}
