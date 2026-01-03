//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportFormat.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Importer.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

#include <Oxygen/Content/Import/fbx/ufbx.h>

namespace oxygen::content::import {

namespace {

  using oxygen::data::AssetKey;
  using oxygen::data::AssetType;

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return std::string_view(s.data, s.length);
  }

  [[nodiscard]] auto MakeDeterministicAssetKey(std::string_view virtual_path)
    -> AssetKey
  {
    const auto bytes = std::as_bytes(
      std::span(virtual_path.data(), static_cast<size_t>(virtual_path.size())));
    const auto digest = oxygen::base::ComputeSha256(bytes);

    AssetKey key {};
    std::copy_n(digest.begin(), key.guid.size(), key.guid.begin());
    return key;
  }

  [[nodiscard]] auto MakeRandomAssetKey() -> AssetKey
  {
    AssetKey key {};
    key.guid = oxygen::data::GenerateAssetGuid();
    return key;
  }

  [[nodiscard]] auto StartsWithIgnoreCase(
    std::string_view str, std::string_view prefix) -> bool
  {
    if (str.size() < prefix.size()) {
      return false;
    }
    return std::equal(
      prefix.begin(), prefix.end(), str.begin(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a))
          == std::tolower(static_cast<unsigned char>(b));
      });
  }

  [[nodiscard]] auto Clamp01(const float v) noexcept -> float
  {
    return std::clamp(v, 0.0F, 1.0F);
  }

  [[nodiscard]] auto ToFloat(const ufbx_real v) noexcept -> float
  {
    return static_cast<float>(v);
  }

  [[nodiscard]] auto BuildMaterialName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kMaterial,
        .ordinal = ordinal,
        .parent_name = {},
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "M_Material_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildMeshName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kMesh,
        .ordinal = ordinal,
        .parent_name = {},
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "G_Mesh_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildSceneNodeName(std::string_view authored,
    const ImportRequest& request, const uint32_t ordinal,
    std::string_view parent_name) -> std::string
  {
    if (request.options.naming_strategy) {
      const NamingContext context {
        .kind = ImportNameKind::kSceneNode,
        .ordinal = ordinal,
        .parent_name = parent_name,
        .source_id = request.source_path.string(),
      };

      if (const auto renamed
        = request.options.naming_strategy->Rename(authored, context);
        renamed.has_value()) {
        return renamed.value();
      }
    }

    if (!authored.empty()) {
      return std::string(authored);
    }

    return "N_Node_" + std::to_string(ordinal);
  }

  [[nodiscard]] auto BuildSceneName(const ImportRequest& request) -> std::string
  {
    const auto stem = request.source_path.stem().string();
    if (!stem.empty()) {
      return stem;
    }
    return "Scene";
  }

  [[nodiscard]] auto NamespaceImportedAssetName(
    const ImportRequest& request, const std::string_view name) -> std::string
  {
    const auto scene_name = BuildSceneName(request);
    if (scene_name.empty()) {
      return std::string(name);
    }
    if (name.empty()) {
      return scene_name;
    }
    return scene_name + "/" + std::string(name);
  }

  auto TruncateAndNullTerminate(
    char* dst, const size_t dst_size, std::string_view s) -> void
  {
    if (dst == nullptr || dst_size == 0) {
      return;
    }

    std::fill_n(dst, dst_size, '\0');
    const auto copy_len = (std::min)(dst_size - 1, s.size());
    std::copy_n(s.data(), copy_len, dst);
  }

  [[nodiscard]] auto AlignUp(const uint64_t value, const uint64_t alignment)
    -> uint64_t
  {
    if (alignment <= 1) {
      return value;
    }

    const auto remainder = value % alignment;
    if (remainder == 0) {
      return value;
    }
    return value + (alignment - remainder);
  }

  [[nodiscard]] auto Sha256ToHex(const oxygen::base::Sha256Digest& digest)
    -> std::string
  {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(digest.size() * 2);
    for (size_t i = 0; i < digest.size(); ++i) {
      const auto b = digest[i];
      out[i * 2 + 0] = kHex[(b >> 4) & 0x0F];
      out[i * 2 + 1] = kHex[b & 0x0F];
    }
    return out;
  }

  [[nodiscard]] auto NormalizeTexturePathId(std::filesystem::path p)
    -> std::string
  {
    if (p.empty()) {
      return {};
    }

    p = p.lexically_normal();
    auto out = p.generic_string();

#if defined(_WIN32)
    std::transform(out.begin(), out.end(), out.begin(), [](const char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif

    return out;
  }

  [[nodiscard]] auto RepackRgba8ToRowPitchAligned(
    const std::span<const std::byte> rgba8_tight, const uint32_t width,
    const uint32_t height, const uint64_t row_pitch_alignment)
    -> std::vector<std::byte>
  {
    constexpr uint64_t kBytesPerPixel = 4;
    const auto tight_row_bytes = static_cast<uint64_t>(width) * kBytesPerPixel;
    const auto row_pitch = AlignUp(tight_row_bytes, row_pitch_alignment);
    const auto total_bytes = row_pitch * static_cast<uint64_t>(height);

    std::vector<std::byte> out;
    out.resize(static_cast<size_t>(total_bytes), std::byte { 0 });

    const auto tight_total_bytes
      = tight_row_bytes * static_cast<uint64_t>(height);
    if (rgba8_tight.size() < static_cast<size_t>(tight_total_bytes)) {
      return out;
    }

    for (uint32_t y = 0; y < height; ++y) {
      const auto src_row_offset
        = static_cast<size_t>(static_cast<uint64_t>(y) * tight_row_bytes);
      const auto dst_row_offset
        = static_cast<size_t>(static_cast<uint64_t>(y) * row_pitch);
      std::copy_n(rgba8_tight.data() + src_row_offset,
        static_cast<size_t>(tight_row_bytes), out.data() + dst_row_offset);
    }
    return out;
  }

  struct TextureEmission final {
    using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;

    std::vector<TextureResourceDesc> table;
    std::vector<std::byte> data;
    std::unordered_map<const ufbx_texture*, uint32_t> index_by_file_texture;
    std::unordered_map<std::string, uint32_t> index_by_texture_id;
    std::unordered_map<std::string, uint32_t> index_by_signature;
  };

  [[nodiscard]] auto MakeTextureSignature(
    const oxygen::data::pak::TextureResourceDesc& desc,
    const std::span<const std::byte> bytes) -> std::string
  {
    const auto digest = oxygen::base::ComputeSha256(bytes);

    std::string signature;
    signature.reserve(128);
    signature.append(Sha256ToHex(digest));
    signature.push_back(':');
    signature.append(std::to_string(desc.width));
    signature.push_back('x');
    signature.append(std::to_string(desc.height));
    signature.push_back(':');
    signature.append(std::to_string(desc.mip_levels));
    signature.push_back(':');
    signature.append(std::to_string(desc.format));
    signature.push_back(':');
    signature.append(std::to_string(desc.alignment));
    signature.push_back(':');
    signature.append(std::to_string(desc.size_bytes));
    return signature;
  }

  [[nodiscard]] auto TryReadWholeFileBytes(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)
      || !std::filesystem::is_regular_file(path, ec)) {
      return std::nullopt;
    }

    const auto size_u64 = std::filesystem::file_size(path, ec);
    if (ec) {
      return std::nullopt;
    }
    if (size_u64 == 0) {
      return std::vector<std::byte> {};
    }
    if (size_u64
      > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
      return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
      return std::nullopt;
    }

    std::vector<std::byte> bytes;
    bytes.resize(static_cast<size_t>(size_u64));
    stream.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    if (!stream.good() && !stream.eof()) {
      return std::nullopt;
    }
    return bytes;
  }

  auto LoadExistingTextureResourcesIfPresent(const std::filesystem::path& root,
    const LooseCookedLayout& layout, CookedContentWriter& out,
    TextureEmission& textures) -> void
  {
    using TextureResourceDesc = oxygen::data::pak::TextureResourceDesc;

    const auto table_path
      = root / std::filesystem::path(layout.TexturesTableRelPath());
    const auto data_path
      = root / std::filesystem::path(layout.TexturesDataRelPath());

    const auto table_bytes_opt = TryReadWholeFileBytes(table_path);
    const auto data_bytes_opt = TryReadWholeFileBytes(data_path);
    if (!table_bytes_opt.has_value() && !data_bytes_opt.has_value()) {
      return;
    }
    if (!table_bytes_opt.has_value() || !data_bytes_opt.has_value()) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "loose_cooked.missing_texture_pair",
        .message
        = "existing cooked root has only one of textures.table/textures.data",
        .source_path = {},
        .object_path = {},
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error(
        "Existing cooked root is missing textures.table or textures.data");
    }

    const auto& table_bytes = *table_bytes_opt;
    const auto& data_bytes = *data_bytes_opt;

    if (table_bytes.size() % sizeof(TextureResourceDesc) != 0U) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "loose_cooked.corrupt_textures_table",
        .message
        = "textures.table size is not a multiple of TextureResourceDesc",
        .source_path = {},
        .object_path = table_path.string(),
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error("Existing textures.table appears corrupt");
    }

    const auto count = table_bytes.size() / sizeof(TextureResourceDesc);
    textures.table.resize(count);
    if (!table_bytes.empty()) {
      std::memcpy(
        textures.table.data(), table_bytes.data(), table_bytes.size());
    }

    textures.data = data_bytes;

    textures.index_by_signature.clear();
    textures.index_by_signature.reserve(textures.table.size());
    for (uint32_t ti = 1; ti < textures.table.size(); ++ti) {
      const auto& desc = textures.table[ti];
      const auto begin = static_cast<uint64_t>(desc.data_offset);
      const auto size = static_cast<uint64_t>(desc.size_bytes);
      if (size == 0) {
        continue;
      }
      if (begin > textures.data.size()
        || size > textures.data.size() - static_cast<size_t>(begin)) {
        ImportDiagnostic diag {
          .severity = ImportSeverity::kError,
          .code = "loose_cooked.corrupt_textures_data",
          .message = "textures.table entry points outside textures.data",
          .source_path = {},
          .object_path = table_path.string(),
        };
        out.AddDiagnostic(std::move(diag));
        throw std::runtime_error("Existing textures.data appears corrupt");
      }

      const auto bytes = std::span<const std::byte>(
        textures.data.data() + static_cast<size_t>(begin),
        static_cast<size_t>(size));
      textures.index_by_signature.emplace(
        MakeTextureSignature(desc, bytes), ti);
    }
    LOG_F(INFO, "Loaded existing textures: count={} bytes={}",
      textures.table.size(), textures.data.size());
  }

  auto LoadExistingBufferResourcesIfPresent(const std::filesystem::path& root,
    const LooseCookedLayout& layout, CookedContentWriter& out,
    std::vector<oxygen::data::pak::BufferResourceDesc>& buffers_table,
    std::vector<std::byte>& buffers_data) -> void
  {
    using BufferResourceDesc = oxygen::data::pak::BufferResourceDesc;

    const auto table_path
      = root / std::filesystem::path(layout.BuffersTableRelPath());
    const auto data_path
      = root / std::filesystem::path(layout.BuffersDataRelPath());

    const auto table_bytes_opt = TryReadWholeFileBytes(table_path);
    const auto data_bytes_opt = TryReadWholeFileBytes(data_path);
    if (!table_bytes_opt.has_value() && !data_bytes_opt.has_value()) {
      return;
    }
    if (!table_bytes_opt.has_value() || !data_bytes_opt.has_value()) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "loose_cooked.missing_buffer_pair",
        .message
        = "existing cooked root has only one of buffers.table/buffers.data",
        .source_path = {},
        .object_path = {},
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error(
        "Existing cooked root is missing buffers.table or buffers.data");
    }

    const auto& table_bytes = *table_bytes_opt;
    const auto& data_bytes = *data_bytes_opt;

    if (table_bytes.size() % sizeof(BufferResourceDesc) != 0U) {
      ImportDiagnostic diag {
        .severity = ImportSeverity::kError,
        .code = "loose_cooked.corrupt_buffers_table",
        .message = "buffers.table size is not a multiple of BufferResourceDesc",
        .source_path = {},
        .object_path = table_path.string(),
      };
      out.AddDiagnostic(std::move(diag));
      throw std::runtime_error("Existing buffers.table appears corrupt");
    }

    const auto count = table_bytes.size() / sizeof(BufferResourceDesc);
    buffers_table.resize(count);
    if (!table_bytes.empty()) {
      std::memcpy(buffers_table.data(), table_bytes.data(), table_bytes.size());
    }

    buffers_data = data_bytes;
    LOG_F(INFO, "Loaded existing buffers: count={} bytes={}",
      buffers_table.size(), buffers_data.size());
  }

  [[nodiscard]] auto ResolveFileTexture(const ufbx_texture* texture)
    -> const ufbx_texture*
  {
    if (texture == nullptr) {
      return nullptr;
    }

    if (texture->file_textures.count > 0) {
      return texture->file_textures.data[0];
    }

    return texture;
  }

  [[nodiscard]] auto TextureIdString(const ufbx_texture& texture)
    -> std::string_view
  {
    if (texture.relative_filename.length > 0) {
      return ToStringView(texture.relative_filename);
    }
    if (texture.filename.length > 0) {
      return ToStringView(texture.filename);
    }
    if (texture.name.length > 0) {
      return ToStringView(texture.name);
    }
    return {};
  }

  [[nodiscard]] auto MakeDeterministicPixelRGBA8(std::string_view id)
    -> std::array<std::byte, 4>
  {
    if (id.empty()) {
      return { std::byte { 0x7F }, std::byte { 0x7F }, std::byte { 0x7F },
        std::byte { 0xFF } };
    }

    const auto bytes
      = std::as_bytes(std::span(id.data(), static_cast<size_t>(id.size())));
    const auto digest = oxygen::base::ComputeSha256(bytes);
    return { std::byte { digest[0] }, std::byte { digest[1] },
      std::byte { digest[2] }, std::byte { 0xFF } };
  }

  auto AppendAligned(std::vector<std::byte>& blob,
    const std::span<const std::byte> bytes, const uint64_t alignment)
    -> uint64_t
  {
    const auto begin = static_cast<uint64_t>(blob.size());
    const auto aligned = AlignUp(begin, alignment);
    if (aligned > begin) {
      blob.resize(static_cast<size_t>(aligned), std::byte { 0 });
    }
    const auto offset = static_cast<uint64_t>(blob.size());
    blob.insert(blob.end(), bytes.begin(), bytes.end());
    return offset;
  }

  auto EnsureFallbackTexture(TextureEmission& out) -> void
  {
    if (!out.table.empty()) {
      return;
    }

    using oxygen::data::pak::TextureResourceDesc;

    constexpr uint64_t kRowPitchAlignment = 256;

    // Index 0 is reserved and must exist.
    // Use a 1x1 white RGBA8, packed with a 256-byte row pitch to satisfy
    // the TextureBinder upload contract.
    const std::array<std::byte, 4> white = { std::byte { 0xFF },
      std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF } };
    const auto packed = RepackRgba8ToRowPitchAligned(
      std::span<const std::byte>(white.data(), white.size()), 1, 1,
      kRowPitchAlignment);
    const auto data_offset = AppendAligned(out.data,
      std::span<const std::byte>(packed.data(), packed.size()),
      kRowPitchAlignment);

    TextureResourceDesc desc {};
    desc.data_offset = data_offset;
    desc.size_bytes = static_cast<uint32_t>(packed.size());
    desc.texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D);
    desc.compression_type = 0;
    desc.width = 1;
    desc.height = 1;
    desc.depth = 1;
    desc.array_layers = 1;
    desc.mip_levels = 1;
    desc.format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm);
    desc.alignment = static_cast<uint32_t>(kRowPitchAlignment);

    out.table.push_back(desc);
  }

  [[nodiscard]] auto GetOrCreateTextureResourceIndex(
    const ImportRequest& request, CookedContentWriter& cooked_out,
    TextureEmission& out, const ufbx_texture* texture) -> uint32_t
  {
    constexpr uint64_t kRowPitchAlignment = 256;

    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return 0;
    }

    EnsureFallbackTexture(out);

    if (const auto it = out.index_by_file_texture.find(file_tex);
      it != out.index_by_file_texture.end()) {
      return it->second;
    }

    const auto id = TextureIdString(*file_tex);
    auto decoded = ImageDecodeResult {};
    const bool is_embedded = (texture != nullptr
      && texture->content.data != nullptr && texture->content.size > 0);

    std::string texture_id;
    std::filesystem::path resolved;

    if (is_embedded) {
      const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(texture->content.data),
        texture->content.size);
      texture_id
        = "embedded:" + Sha256ToHex(oxygen::base::ComputeSha256(bytes));
      if (const auto it = out.index_by_texture_id.find(texture_id);
        it != out.index_by_texture_id.end()) {
        out.index_by_file_texture.insert_or_assign(file_tex, it->second);
        return it->second;
      }
      decoded = DecodeImageRgba8FromMemory(bytes);
    } else {
      auto rel = ToStringView(file_tex->relative_filename);
      auto abs = ToStringView(file_tex->filename);

      if (rel.empty() && abs.empty()) {
        const auto rel_prop
          = ufbx_find_string(&file_tex->props, "RelativeFilename", {});
        const auto abs_prop
          = ufbx_find_string(&file_tex->props, "FileName", {});
        if (rel_prop.length > 0) {
          rel = ToStringView(rel_prop);
        }
        if (abs_prop.length > 0) {
          abs = ToStringView(abs_prop);
        }
      }

      if (rel.empty() && abs.empty() && texture != nullptr) {
        rel = ToStringView(texture->relative_filename);
        abs = ToStringView(texture->filename);
        if (rel.empty() && abs.empty()) {
          const auto rel_prop
            = ufbx_find_string(&texture->props, "RelativeFilename", {});
          const auto abs_prop
            = ufbx_find_string(&texture->props, "FileName", {});
          if (rel_prop.length > 0) {
            rel = ToStringView(rel_prop);
          }
          if (abs_prop.length > 0) {
            abs = ToStringView(abs_prop);
          }
        }
      }
      if (!rel.empty()) {
        resolved = request.source_path.parent_path()
          / std::filesystem::path(std::string(rel));
      } else if (!abs.empty()) {
        const auto abs_path = std::filesystem::path(std::string(abs));
        resolved = abs_path.is_absolute()
          ? abs_path
          : (request.source_path.parent_path() / abs_path);
      }

      texture_id = !resolved.empty() ? NormalizeTexturePathId(resolved)
                                     : std::string(id);

      if (!texture_id.empty()) {
        if (const auto it = out.index_by_texture_id.find(texture_id);
          it != out.index_by_texture_id.end()) {
          out.index_by_file_texture.insert_or_assign(file_tex, it->second);
          return it->second;
        }
      }

      if (!resolved.empty()) {
        decoded = DecodeImageRgba8FromFile(resolved);
      } else {
        decoded.error = "texture has no filename or embedded content";
      }
    }

    std::span<const std::byte> pixels;
    uint32_t width = 1;
    uint32_t height = 1;
    std::array<std::byte, 4> placeholder_pixel = {};
    bool used_placeholder = false;

    if (decoded.Succeeded() && decoded.image->width > 0
      && decoded.image->height > 0 && !decoded.image->pixels.empty()) {
      pixels = std::span<const std::byte>(
        decoded.image->pixels.data(), decoded.image->pixels.size());
      width = decoded.image->width;
      height = decoded.image->height;
    } else {
      used_placeholder = true;
      if (!decoded.error.empty()) {
        ImportDiagnostic diag {
          .severity = ImportSeverity::kWarning,
          .code = "fbx.texture_decode_failed",
          .message = "failed to decode texture '" + std::string(id)
            + "': " + decoded.error + "; using 1x1 placeholder",
          .source_path = request.source_path.string(),
          .object_path = std::string(id),
        };
        cooked_out.AddDiagnostic(std::move(diag));
      }

      placeholder_pixel = MakeDeterministicPixelRGBA8(id);
      pixels = std::span<const std::byte>(
        placeholder_pixel.data(), placeholder_pixel.size());
      width = 1;
      height = 1;
    }

    const auto packed_pixels
      = RepackRgba8ToRowPitchAligned(pixels, width, height, kRowPitchAlignment);

    TextureEmission::TextureResourceDesc desc {};
    desc.data_offset = 0;
    desc.size_bytes = static_cast<uint32_t>(packed_pixels.size());
    desc.texture_type = static_cast<uint8_t>(oxygen::TextureType::kTexture2D);
    desc.compression_type = 0;
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.array_layers = 1;
    desc.mip_levels = 1;
    desc.format = static_cast<uint8_t>(oxygen::Format::kRGBA8UNorm);
    desc.alignment = static_cast<uint32_t>(kRowPitchAlignment);

    const auto signature = MakeTextureSignature(desc,
      std::span<const std::byte>(packed_pixels.data(), packed_pixels.size()));
    if (const auto it = out.index_by_signature.find(signature);
      it != out.index_by_signature.end()) {
      const auto existing_index = it->second;
      out.index_by_file_texture.insert_or_assign(file_tex, existing_index);
      if (!texture_id.empty()) {
        out.index_by_texture_id.insert_or_assign(texture_id, existing_index);
      }
      LOG_F(INFO,
        "Reuse texture '{}' ({}x{}, bytes={}, embedded={}, placeholder={}) -> "
        "index {}",
        std::string(id).c_str(), width, height, pixels.size(), is_embedded,
        used_placeholder, existing_index);
      return existing_index;
    }

    const auto data_offset = AppendAligned(out.data,
      std::span<const std::byte>(packed_pixels.data(), packed_pixels.size()),
      kRowPitchAlignment);

    desc.data_offset = data_offset;

    LOG_F(INFO,
      "Emit texture '{}' ({}x{}, bytes={}, embedded={}, placeholder={}) -> "
      "index {}",
      std::string(id).c_str(), width, height, pixels.size(), is_embedded,
      used_placeholder, out.table.size());

    const auto index = static_cast<uint32_t>(out.table.size());
    out.table.push_back(desc);
    out.index_by_file_texture.insert_or_assign(file_tex, index);
    if (!texture_id.empty()) {
      out.index_by_texture_id.insert_or_assign(texture_id, index);
    }
    out.index_by_signature.emplace(signature, index);
    return index;
  }

  [[nodiscard]] auto SelectBaseColorTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.base_color;
    // Some exporters leave `texture_enabled` unset even when a valid texture
    // connection exists. Prefer the pointer presence while honoring
    // `feature_disabled`.
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    const auto& fbx = material.fbx.diffuse_color;
    if (!fbx.feature_disabled && fbx.texture != nullptr) {
      return fbx.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectNormalTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.normal_map;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    const auto& fbx = material.fbx.normal_map;
    if (!fbx.feature_disabled && fbx.texture != nullptr) {
      return fbx.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectMetallicTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.metalness;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectRoughnessTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.roughness;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectAmbientOcclusionTexture(
    const ufbx_material& material) -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.ambient_occlusion;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto EngineWorldTargetAxes() -> ufbx_coordinate_axes
  {
    // Oxygen engine world conventions (Oxygen/Core/Constants.h):
    //   - Right-handed
    //   - Z-up
    //   - Forward = -Y
    //
    // ufbx `front` axis is the "Back" direction (opposite of Forward).
    // To map Source Forward (-Z) to Oxygen Forward (-Y), we need a rotation.
    //
    // We construct a Right-Handed basis for the target space:
    //   Right = -X (maps Source Right +X to -X)
    //   Up    = +Z (maps Source Up +Y to +Z)
    //   Front = +Y (maps Source Back +Z to +Y)
    //
    // Check Determinant(-X, Z, Y):
    //   -X = (-1, 0, 0)
    //   Z  = ( 0, 0, 1)
    //   Y  = ( 0, 1, 0)
    //   Det = -1 * (0*0 - 1*1) = 1. (Right-Handed)
    //
    // Mapping check:
    //   Source Forward (-Z) -> M * (0,0,-1) = - (Col 2) = - (+Y) = -Y.
    //   This matches Oxygen Forward (-Y).
    //
    // Note: Oxygen's "Right" constant is +X, but for a character facing -Y,
    // the "Natural Right" is -X. This basis aligns the model's natural right
    // to -X.
    return ufbx_coordinate_axes {
      .right = UFBX_COORDINATE_AXIS_NEGATIVE_X,
      .up = UFBX_COORDINATE_AXIS_POSITIVE_Z,
      .front = UFBX_COORDINATE_AXIS_POSITIVE_Y,
    };
  }

  [[nodiscard]] auto EngineCameraTargetAxes() -> ufbx_coordinate_axes
  {
    // Oxygen camera/view conventions (Oxygen/Core/Constants.h):
    //   view forward = -Z, up = +Y, right = +X
    // ufbx stores `front` as the opposite of forward.
    return ufbx_coordinate_axes {
      .right = UFBX_COORDINATE_AXIS_POSITIVE_X,
      .up = UFBX_COORDINATE_AXIS_POSITIVE_Y,
      .front = UFBX_COORDINATE_AXIS_POSITIVE_Z,
    };
  }

  [[nodiscard]] auto ComputeTargetUnitMeters(
    const CoordinateConversionPolicy& policy) -> std::optional<ufbx_real>
  {
    switch (policy.unit_normalization) {
    case UnitNormalizationPolicy::kNormalizeToMeters:
      return 1.0;
    case UnitNormalizationPolicy::kPreserveSource:
      return std::nullopt;
    case UnitNormalizationPolicy::kApplyCustomFactor: {
      if (!(policy.custom_unit_scale > 0.0F)) {
        return std::nullopt;
      }
      return static_cast<ufbx_real>(1.0 / policy.custom_unit_scale);
    }
    }

    return std::nullopt;
  }

  [[nodiscard]] auto SwapYZMatrix() -> ufbx_matrix
  {
    // Permutation matrix that swaps Y/Z components.
    // `ufbx_matrix` is column-major: cols[0..2] are basis vectors.
    return ufbx_matrix {
      .cols = {
        { 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0 },
        { 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0 },
      },
    };
  }

  [[nodiscard]] auto ApplySwapYZIfEnabled(
    const CoordinateConversionPolicy& policy, const ufbx_transform& t)
    -> ufbx_transform
  {
    if (!policy.swap_yz_axes) {
      return t;
    }

    // Apply a similarity transform: M' = P * M * P^{-1}.
    // For a pure axis permutation, P^{-1} == P.
    const auto p = SwapYZMatrix();
    const auto m = ufbx_transform_to_matrix(&t);
    const auto pm = ufbx_matrix_mul(&p, &m);
    const auto pmp = ufbx_matrix_mul(&pm, &p);
    return ufbx_matrix_to_transform(&pmp);
  }

  [[nodiscard]] auto ApplySwapYZIfEnabled(
    const CoordinateConversionPolicy& policy, const ufbx_vec3 v) -> ufbx_vec3
  {
    if (!policy.swap_yz_axes) {
      return v;
    }

    const auto p = SwapYZMatrix();
    return ufbx_transform_position(&p, v);
  }

  [[nodiscard]] auto ApplySwapYZDirIfEnabled(
    const CoordinateConversionPolicy& policy, const ufbx_vec3 v) -> ufbx_vec3
  {
    if (!policy.swap_yz_axes) {
      return v;
    }

    const auto p = SwapYZMatrix();
    return ufbx_transform_direction(&p, v);
  }

  [[nodiscard]] auto ApplySwapYZIfEnabled(
    const CoordinateConversionPolicy& policy, const ufbx_matrix& m)
    -> ufbx_matrix
  {
    if (!policy.swap_yz_axes) {
      return m;
    }

    const auto p = SwapYZMatrix();
    const auto pm = ufbx_matrix_mul(&p, &m);
    const auto pmp = ufbx_matrix_mul(&pm, &p);
    return pmp;
  }

  [[nodiscard]] auto ToGlmMat4(const ufbx_matrix& m) -> glm::mat4
  {
    // ufbx_matrix is an affine 4x3 matrix in column-major form.
    // cols[0..2] are basis vectors, cols[3] is translation.
    glm::mat4 out(1.0F);
    // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
    out[0] = glm::vec4(static_cast<float>(m.cols[0].x),
      static_cast<float>(m.cols[0].y), static_cast<float>(m.cols[0].z), 0.0F);
    out[1] = glm::vec4(static_cast<float>(m.cols[1].x),
      static_cast<float>(m.cols[1].y), static_cast<float>(m.cols[1].z), 0.0F);
    out[2] = glm::vec4(static_cast<float>(m.cols[2].x),
      static_cast<float>(m.cols[2].y), static_cast<float>(m.cols[2].z), 0.0F);
    out[3] = glm::vec4(static_cast<float>(m.cols[3].x),
      static_cast<float>(m.cols[3].y), static_cast<float>(m.cols[3].z), 1.0F);
    // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
    return out;
  }

  struct ImportedGeometry final {
    const ufbx_mesh* mesh = nullptr;
    AssetKey key = {};
  };

  class FbxImporter final : public Importer {
  public:
    [[nodiscard]] auto Name() const noexcept -> std::string_view override
    {
      return "FbxImporter";
    }

    [[nodiscard]] auto Supports(const ImportFormat format) const noexcept
      -> bool override
    {
      return format == ImportFormat::kFbx;
    }

    auto Import(const ImportRequest& request, CookedContentWriter& out)
      -> void override
    {
      const auto source_path_str = request.source_path.string();
      LOG_SCOPE_F(INFO, "FbxImporter::Import {}", source_path_str.c_str());

      const auto cooked_root = request.cooked_root.value_or(
        std::filesystem::absolute(request.source_path.parent_path()));

      ufbx_load_opts opts {};
      ufbx_error error {};

      // Always normalize coordinate system to Oxygen engine space.
      opts.target_axes = EngineWorldTargetAxes();
      opts.target_camera_axes = EngineCameraTargetAxes();

      // FBX nodes may contain "geometry transforms" that affect only the
      // attached attribute (mesh/camera/light), not children. Our cooked scene
      // representation does not currently model these separately, so request
      // ufbx to represent them using helper nodes.
      opts.geometry_transform_handling
        = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;

      // Prefer modifying geometry to ensure vertex positions (and compatible
      // linear terms) are scaled/rotated as required by import policy.
      opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

      const auto& coordinate_policy = request.options.coordinate;
      if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor) {
        if (!(coordinate_policy.custom_unit_scale > 0.0F)) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.invalid_custom_unit_scale",
            .message = "custom_unit_scale must be > 0 when using "
                       "UnitNormalizationPolicy::kApplyCustomFactor",
            .source_path = source_path_str,
            .object_path = {},
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error(
            "FBX import invalid custom_unit_scale (must be > 0)");
        }
      }

      if (const auto target_unit_meters
        = ComputeTargetUnitMeters(coordinate_policy);
        target_unit_meters.has_value()) {
        opts.target_unit_meters = *target_unit_meters;
      }

      opts.generate_missing_normals = true;

      ufbx_scene* scene
        = ufbx_load_file(source_path_str.c_str(), &opts, &error);
      if (scene == nullptr) {
        const auto desc = ToStringView(error.description);
        ImportDiagnostic diag {
          .severity = ImportSeverity::kError,
          .code = "fbx.parse_failed",
          .message = std::string(desc),
          .source_path = source_path_str,
          .object_path = {},
        };
        out.AddDiagnostic(std::move(diag));
        throw std::runtime_error("FBX parse failed: " + std::string(desc));
      }

      const auto material_count = static_cast<uint32_t>(scene->materials.count);
      const auto mesh_count = static_cast<uint32_t>(scene->meshes.count);
      const auto node_count = static_cast<uint32_t>(scene->nodes.count);
      // ufbx also keeps a direct list of camera objects; logging both helps
      // distinguish "no camera exported" from "camera exists but not attached
      // to any node" scenarios.
      const auto camera_count = static_cast<uint32_t>(scene->cameras.count);
      LOG_F(INFO,
        "FBX scene loaded: {} materials, {} meshes, {} nodes, {} cameras. "
        "SwapYZ={}",
        material_count, mesh_count, node_count, camera_count,
        request.options.coordinate.swap_yz_axes);

      const auto want_materials
        = (request.options.import_content & ImportContentFlags::kMaterials)
        != ImportContentFlags::kNone;

      const auto want_geometry
        = (request.options.import_content & ImportContentFlags::kGeometry)
        != ImportContentFlags::kNone;

      const auto want_scene
        = (request.options.import_content & ImportContentFlags::kScene)
        != ImportContentFlags::kNone;

      const auto want_textures
        = (request.options.import_content & ImportContentFlags::kTextures)
        != ImportContentFlags::kNone;

      if (want_scene && !want_geometry) {
        ImportDiagnostic diag {
          .severity = ImportSeverity::kError,
          .code = "fbx.scene.requires_geometry",
          .message = "FBX scene import currently requires geometry emission",
          .source_path = source_path_str,
          .object_path = {},
        };
        out.AddDiagnostic(std::move(diag));
        throw std::runtime_error("FBX scene import requires geometry");
      }

      TextureEmission textures;
      if (want_textures) {
        LoadExistingTextureResourcesIfPresent(
          cooked_root, request.loose_cooked_layout, out, textures);
        EnsureFallbackTexture(textures);
      }

      uint32_t written_materials = 0;
      std::vector<AssetKey> material_keys;
      if (want_materials) {
        WriteMaterials_(*scene, request, out, textures, want_textures,
          written_materials, material_keys);
      }

      if (written_materials > 0) {
        out.OnMaterialsWritten(written_materials);
      }

      if (want_textures) {
        WriteTextures_(request, out, textures);
      }

      uint32_t written_geometry = 0;
      std::vector<ImportedGeometry> imported_geometry;
      if (want_geometry) {
        WriteGeometry_(*scene, request, out, material_keys, imported_geometry,
          written_geometry, want_textures);
      }

      if (written_geometry > 0) {
        out.OnGeometryWritten(written_geometry);
      }

      uint32_t written_scenes = 0;
      if (want_scene) {
        WriteScene_(*scene, request, out, imported_geometry, written_scenes);
      }

      if (written_scenes > 0) {
        out.OnScenesWritten(written_scenes);
      }

      ufbx_free_scene(scene);
    }

  private:
    static auto WriteMaterials_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      TextureEmission& textures, const bool want_textures,
      uint32_t& written_materials, std::vector<AssetKey>& out_keys) -> void
    {
      const auto count = static_cast<uint32_t>(scene.materials.count);
      if (count == 0) {
        const auto name = BuildMaterialName("M_Default", request, 0);
        const auto key = WriteOneMaterial_(
          request, out, name, 0, nullptr, textures, want_textures);
        out_keys.push_back(key);
        written_materials += 1;
        return;
      }

      for (uint32_t i = 0; i < count; ++i) {
        const auto* mat = scene.materials.data[i];
        const auto authored_name
          = (mat != nullptr) ? ToStringView(mat->name) : std::string_view {};
        const auto name = BuildMaterialName(authored_name, request, i);

        const auto key = WriteOneMaterial_(
          request, out, name, i, mat, textures, want_textures);
        out_keys.push_back(key);
        written_materials += 1;
      }
    }

    [[nodiscard]] static auto WriteOneMaterial_(const ImportRequest& request,
      CookedContentWriter& out, std::string_view material_name,
      const uint32_t ordinal, const ufbx_material* material,
      TextureEmission& textures, const bool want_textures) -> AssetKey
    {
      const auto storage_name
        = NamespaceImportedAssetName(request, material_name);
      const auto virtual_path
        = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

      const auto relpath
        = request.loose_cooked_layout.DescriptorDirFor(AssetType::kMaterial)
        + "/" + LooseCookedLayout::MaterialDescriptorFileName(storage_name);

      AssetKey key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        key = MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        key = MakeRandomAssetKey();
        break;
      }

      oxygen::data::pak::MaterialAssetDesc desc {};
      desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
      TruncateAndNullTerminate(
        desc.header.name, std::size(desc.header.name), material_name);
      desc.material_domain
        = static_cast<uint8_t>(oxygen::data::MaterialDomain::kOpaque);
      desc.flags = oxygen::data::pak::kMaterialFlag_NoTextureSampling;

      if (material != nullptr) {
        // Scalar PBR factors (used even when texture sampling is disabled).
        ufbx_vec4 base = { 1.0, 1.0, 1.0, 1.0 };
        if (material->pbr.base_color.has_value
          && material->pbr.base_color.value_components >= 3) {
          base = material->pbr.base_color.value_vec4;
        } else if (material->fbx.diffuse_color.has_value
          && material->fbx.diffuse_color.value_components >= 3) {
          const auto dc = material->fbx.diffuse_color.value_vec3;
          base = { dc.x, dc.y, dc.z, 1.0 };
        }

        float base_factor = 1.0F;
        if (material->pbr.base_factor.has_value) {
          base_factor = Clamp01(ToFloat(material->pbr.base_factor.value_real));
        } else if (material->fbx.diffuse_factor.has_value) {
          base_factor
            = Clamp01(ToFloat(material->fbx.diffuse_factor.value_real));
        }

        desc.base_color[0] = Clamp01(ToFloat(base.x) * base_factor);
        desc.base_color[1] = Clamp01(ToFloat(base.y) * base_factor);
        desc.base_color[2] = Clamp01(ToFloat(base.z) * base_factor);
        desc.base_color[3] = Clamp01(ToFloat(base.w) * base_factor);

        if (material->pbr.metalness.has_value) {
          desc.metalness = oxygen::data::Unorm16 { Clamp01(
            ToFloat(material->pbr.metalness.value_real)) };
        }

        float specular_factor = 1.0F;

        const auto shading_model = ToStringView(material->shading_model_name);
        const auto fbx_material_name = ToStringView(material->name);

        bool is_lambert = (material->shader_type == UFBX_SHADER_FBX_LAMBERT);
        if (!is_lambert) {
          // Fallback check for string name if ufbx didn't classify it
          if (shading_model == "Lambert" || shading_model == "lambert") {
            is_lambert = true;
          }
          // Heuristic: if the material name starts with "lambert" (e.g.
          // "lambert1"), treat it as Lambert even if the shading model says
          // Phong. This fixes issues with default materials in FBX exported
          // from Maya/etc.
          else if (StartsWithIgnoreCase(fbx_material_name, "lambert")) {
            is_lambert = true;
          }
        }

        LOG_F(INFO, "Material '{}': shader_type={} model='{}' is_lambert={}",
          fbx_material_name, (int)material->shader_type, shading_model,
          is_lambert);

        // Lambert materials in FBX often have garbage/default specular values.
        // UE5 imports them as 0.5 (default PBR specular).
        if (is_lambert) {
          specular_factor = 0.5F;
        } else {
          if (material->pbr.specular_factor.has_value) {
            specular_factor
              = Clamp01(ToFloat(material->pbr.specular_factor.value_real));
          } else if (material->fbx.specular_factor.has_value) {
            specular_factor
              = Clamp01(ToFloat(material->fbx.specular_factor.value_real));
          }

          // Modulate by specular color intensity if present.
          // This handles cases where specular is defined by color instead of
          // factor, or both.
          if (material->pbr.specular_color.has_value) {
            const auto& c = material->pbr.specular_color.value_vec4;
            const float intensity
              = (std::max)({ ToFloat(c.x), ToFloat(c.y), ToFloat(c.z) });
            specular_factor *= intensity;
          } else if (material->fbx.specular_color.has_value) {
            const auto& c = material->fbx.specular_color.value_vec4;
            const float intensity
              = (std::max)({ ToFloat(c.x), ToFloat(c.y), ToFloat(c.z) });
            specular_factor *= intensity;
          }
        }

        desc.specular_factor
          = oxygen::data::Unorm16 { Clamp01(specular_factor) };

        float roughness = 1.0F;
        if (material->pbr.roughness.has_value) {
          roughness = Clamp01(ToFloat(material->pbr.roughness.value_real));
        }
        if (material->features.roughness_as_glossiness.enabled) {
          roughness = 1.0F - roughness;
        }
        desc.roughness = oxygen::data::Unorm16 { Clamp01(roughness) };

        if (material->pbr.ambient_occlusion.has_value) {
          desc.ambient_occlusion = oxygen::data::Unorm16 { Clamp01(
            ToFloat(material->pbr.ambient_occlusion.value_real)) };
        }

        if (material->pbr.normal_map.has_value) {
          desc.normal_scale
            = (std::max)(0.0F, ToFloat(material->pbr.normal_map.value_real));
        } else if (material->fbx.bump_factor.has_value) {
          desc.normal_scale
            = (std::max)(0.0F, ToFloat(material->fbx.bump_factor.value_real));
        }

        if (material->features.double_sided.enabled) {
          desc.flags |= oxygen::data::pak::kMaterialFlag_DoubleSided;
        }
        if (material->features.unlit.enabled) {
          desc.flags |= oxygen::data::pak::kMaterialFlag_Unlit;
        }
      }

      if (want_textures && material != nullptr) {
        const auto base_color_tex = SelectBaseColorTexture(*material);
        const auto normal_tex = SelectNormalTexture(*material);
        const auto metallic_tex = SelectMetallicTexture(*material);
        const auto roughness_tex = SelectRoughnessTexture(*material);
        const auto ao_tex = SelectAmbientOcclusionTexture(*material);

        const auto base_color_index = GetOrCreateTextureResourceIndex(
          request, out, textures, base_color_tex);
        const auto normal_index
          = GetOrCreateTextureResourceIndex(request, out, textures, normal_tex);
        const auto metallic_index = GetOrCreateTextureResourceIndex(
          request, out, textures, metallic_tex);
        const auto roughness_index = GetOrCreateTextureResourceIndex(
          request, out, textures, roughness_tex);
        const auto ao_index
          = GetOrCreateTextureResourceIndex(request, out, textures, ao_tex);

        desc.base_color_texture = base_color_index;
        desc.normal_texture = normal_index;
        desc.metallic_texture = metallic_index;
        desc.roughness_texture = roughness_index;
        desc.ambient_occlusion_texture = ao_index;

        if (base_color_index != 0 || normal_index != 0 || metallic_index != 0
          || roughness_index != 0 || ao_index != 0) {
          desc.flags &= ~oxygen::data::pak::kMaterialFlag_NoTextureSampling;
        }
      }

      oxygen::serio::MemoryStream stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);
      (void)writer.WriteBlob(std::as_bytes(
        std::span<const oxygen::data::pak::MaterialAssetDesc, 1>(&desc, 1)));

      const auto bytes = stream.Data();

      LOG_F(INFO, "Emit material {} '{}' -> {}", ordinal,
        std::string(material_name).c_str(), relpath.c_str());

      out.WriteAssetDescriptor(
        key, AssetType::kMaterial, virtual_path, relpath, bytes);

      return key;
    }

    static auto WriteTextures_(const ImportRequest& request,
      CookedContentWriter& out, const TextureEmission& textures) -> void
    {
      using oxygen::data::loose_cooked::v1::FileKind;
      using oxygen::data::pak::TextureResourceDesc;

      if (textures.table.empty()) {
        return;
      }

      LOG_F(INFO, "Emit textures table: count={} bytes={} -> '{}' / '{}'",
        textures.table.size(), textures.data.size(),
        request.loose_cooked_layout.TexturesTableRelPath().c_str(),
        request.loose_cooked_layout.TexturesDataRelPath().c_str());

      oxygen::serio::MemoryStream table_stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> table_writer(
        table_stream);
      const auto pack = table_writer.ScopedAlignment(1);
      (void)table_writer.WriteBlob(
        std::as_bytes(std::span<const TextureResourceDesc>(textures.table)));

      out.WriteFile(FileKind::kTexturesTable,
        request.loose_cooked_layout.TexturesTableRelPath(),
        table_stream.Data());

      out.WriteFile(FileKind::kTexturesData,
        request.loose_cooked_layout.TexturesDataRelPath(),
        std::span<const std::byte>(textures.data.data(), textures.data.size()));
    }

    static auto WriteGeometry_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      const std::vector<AssetKey>& material_keys,
      std::vector<ImportedGeometry>& out_geometry, uint32_t& written_geometry,
      const bool want_textures) -> void
    {
      using oxygen::data::BufferResource;
      using oxygen::data::MeshType;
      using oxygen::data::Vertex;
      using oxygen::data::loose_cooked::v1::FileKind;
      using oxygen::data::pak::BufferResourceDesc;
      using oxygen::data::pak::GeometryAssetDesc;
      using oxygen::data::pak::MeshDesc;
      using oxygen::data::pak::MeshViewDesc;
      using oxygen::data::pak::SubMeshDesc;

      std::vector<BufferResourceDesc> buffers_table;
      std::vector<std::byte> buffers_data;

      const auto cooked_root = request.cooked_root.value_or(
        std::filesystem::absolute(request.source_path.parent_path()));
      LoadExistingBufferResourcesIfPresent(cooked_root,
        request.loose_cooked_layout, out, buffers_table, buffers_data);

      std::unordered_map<std::string, uint32_t> buffer_index_by_signature;
      buffer_index_by_signature.reserve(buffers_table.size());
      for (uint32_t bi = 0; bi < buffers_table.size(); ++bi) {
        const auto& desc = buffers_table[bi];
        const auto begin = static_cast<uint64_t>(desc.data_offset);
        const auto size = static_cast<uint64_t>(desc.size_bytes);
        if (size == 0) {
          continue;
        }
        if (begin > buffers_data.size()
          || size > buffers_data.size() - static_cast<size_t>(begin)) {
          continue;
        }

        const auto bytes = std::span<const std::byte>(
          buffers_data.data() + static_cast<size_t>(begin),
          static_cast<size_t>(size));
        const auto digest = oxygen::base::ComputeSha256(bytes);

        std::string signature;
        signature.reserve(96);
        signature.append(Sha256ToHex(digest));
        signature.push_back(':');
        signature.append(std::to_string(desc.usage_flags));
        signature.push_back(':');
        signature.append(std::to_string(desc.element_stride));
        signature.push_back(':');
        signature.append(std::to_string(desc.element_format));
        signature.push_back(':');
        signature.append(std::to_string(desc.size_bytes));

        buffer_index_by_signature.emplace(std::move(signature), bi);
      }

      auto effective_material_keys = material_keys;
      if (effective_material_keys.empty()) {
        const auto count = static_cast<uint32_t>(scene.materials.count);
        if (count == 0) {
          const auto name = BuildMaterialName("M_Default", request, 0);
          const auto storage_name = NamespaceImportedAssetName(request, name);
          const auto virtual_path
            = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

          AssetKey key {};
          switch (request.options.asset_key_policy) {
          case AssetKeyPolicy::kDeterministicFromVirtualPath:
            key = MakeDeterministicAssetKey(virtual_path);
            break;
          case AssetKeyPolicy::kRandom:
            key = MakeRandomAssetKey();
            break;
          }

          effective_material_keys.push_back(key);
        } else {
          effective_material_keys.reserve(count);
          for (uint32_t i = 0; i < count; ++i) {
            const auto* mat = scene.materials.data[i];
            const auto authored_name = (mat != nullptr)
              ? ToStringView(mat->name)
              : std::string_view {};
            const auto name = BuildMaterialName(authored_name, request, i);
            const auto storage_name = NamespaceImportedAssetName(request, name);
            const auto virtual_path
              = request.loose_cooked_layout.MaterialVirtualPath(storage_name);

            AssetKey key {};
            switch (request.options.asset_key_policy) {
            case AssetKeyPolicy::kDeterministicFromVirtualPath:
              key = MakeDeterministicAssetKey(virtual_path);
              break;
            case AssetKeyPolicy::kRandom:
              key = MakeRandomAssetKey();
              break;
            }

            effective_material_keys.push_back(key);
          }
        }
      }

      auto append_bytes = [&](std::span<const std::byte> bytes,
                            const uint64_t alignment) {
        const auto begin = static_cast<uint64_t>(buffers_data.size());
        const auto aligned = AlignUp(begin, alignment);
        if (aligned > begin) {
          buffers_data.resize(static_cast<size_t>(aligned), std::byte { 0 });
        }
        const auto offset = static_cast<uint64_t>(buffers_data.size());
        buffers_data.insert(buffers_data.end(), bytes.begin(), bytes.end());
        return offset;
      };

      auto GetOrCreateBufferIndex
        = [&](const std::span<const std::byte> bytes, const uint64_t alignment,
            const uint32_t usage_flags, const uint32_t element_stride,
            const uint8_t element_format) -> uint32_t {
        if (bytes.empty()) {
          return 0;
        }

        const auto digest = oxygen::base::ComputeSha256(bytes);

        std::string signature;
        signature.reserve(96);
        signature.append(Sha256ToHex(digest));
        signature.push_back(':');
        signature.append(std::to_string(usage_flags));
        signature.push_back(':');
        signature.append(std::to_string(element_stride));
        signature.push_back(':');
        signature.append(std::to_string(element_format));
        signature.push_back(':');
        signature.append(std::to_string(bytes.size()));

        if (const auto it = buffer_index_by_signature.find(signature);
          it != buffer_index_by_signature.end()) {
          return it->second;
        }

        BufferResourceDesc desc {};
        desc.data_offset = append_bytes(bytes, alignment);
        desc.size_bytes = static_cast<uint32_t>(bytes.size());
        desc.usage_flags = usage_flags;
        desc.element_stride = element_stride;
        desc.element_format = element_format;

        const auto index = static_cast<uint32_t>(buffers_table.size());
        buffers_table.push_back(desc);
        buffer_index_by_signature.emplace(std::move(signature), index);
        return index;
      };

      const auto mesh_count = static_cast<uint32_t>(scene.meshes.count);
      for (uint32_t i = 0; i < mesh_count; ++i) {
        const auto* mesh = scene.meshes.data[i];
        if (mesh == nullptr || mesh->num_indices == 0 || mesh->num_faces == 0) {
          continue;
        }

        std::unordered_map<const ufbx_material*, uint32_t>
          scene_material_index_by_ptr;
        scene_material_index_by_ptr.reserve(
          static_cast<size_t>(scene.materials.count));

        std::unordered_map<const ufbx_material*, AssetKey> material_key_by_ptr;
        material_key_by_ptr.reserve(static_cast<size_t>(scene.materials.count));

        for (uint32_t mat_i = 0; mat_i < scene.materials.count; ++mat_i) {
          const auto* mat = scene.materials.data[mat_i];
          if (mat == nullptr) {
            continue;
          }
          scene_material_index_by_ptr.emplace(mat, mat_i);
          if (mat_i < effective_material_keys.size()) {
            material_key_by_ptr.emplace(mat, effective_material_keys[mat_i]);
          }
        }

        if (!mesh->vertex_position.exists
          || mesh->vertex_position.values.data == nullptr
          || mesh->vertex_position.indices.data == nullptr) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.mesh.missing_positions",
            .message = "FBX mesh is missing vertex positions",
            .source_path = request.source_path.string(),
            .object_path = std::string(ToStringView(mesh->name)),
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error("FBX mesh missing positions");
        }

        const auto authored_name = ToStringView(mesh->name);
        const auto mesh_name = BuildMeshName(authored_name, request, i);

        const bool has_uv = mesh->vertex_uv.exists
          && mesh->vertex_uv.values.data != nullptr
          && mesh->vertex_uv.indices.data != nullptr;

        if (!has_uv && want_textures && mesh->materials.data != nullptr
          && mesh->materials.count > 0) {
          bool has_any_material_texture = false;
          for (uint32_t mi = 0; mi < mesh->materials.count; ++mi) {
            const auto* mat = mesh->materials.data[mi];
            if (mat == nullptr) {
              continue;
            }
            if (SelectBaseColorTexture(*mat) != nullptr
              || SelectNormalTexture(*mat) != nullptr
              || SelectMetallicTexture(*mat) != nullptr
              || SelectRoughnessTexture(*mat) != nullptr
              || SelectAmbientOcclusionTexture(*mat) != nullptr) {
              has_any_material_texture = true;
              break;
            }
          }

          if (has_any_material_texture) {
            ImportDiagnostic diag {
              .severity = ImportSeverity::kWarning,
              .code = "fbx.mesh.missing_uvs",
              .message = "mesh has materials with textures but no UVs; "
                         "texture sampling and normal mapping may be "
                         "incorrect",
              .source_path = request.source_path.string(),
              .object_path = std::string(mesh_name),
            };
            out.AddDiagnostic(std::move(diag));
          }
        }

        std::vector<Vertex> vertices;
        vertices.reserve(mesh->num_indices);

        float bbox_min[3] = {
          (std::numeric_limits<float>::max)(),
          (std::numeric_limits<float>::max)(),
          (std::numeric_limits<float>::max)(),
        };
        float bbox_max[3] = {
          (std::numeric_limits<float>::lowest)(),
          (std::numeric_limits<float>::lowest)(),
          (std::numeric_limits<float>::lowest)(),
        };

        for (size_t idx = 0; idx < mesh->num_indices; ++idx) {
          auto p = mesh->vertex_position[idx];
          p = ApplySwapYZIfEnabled(request.options.coordinate, p);

          Vertex v {
            .position = { static_cast<float>(p.x), static_cast<float>(p.y),
              static_cast<float>(p.z) },
            .normal = { 0.0f, 1.0f, 0.0f },
            .texcoord = { 0.0f, 0.0f },
            .tangent = { 1.0f, 0.0f, 0.0f },
            .bitangent = { 0.0f, 0.0f, 1.0f },
            .color = { 1.0f, 1.0f, 1.0f, 1.0f },
          };

          if (mesh->vertex_normal.exists
            && mesh->vertex_normal.values.data != nullptr
            && mesh->vertex_normal.indices.data != nullptr) {
            auto n = mesh->vertex_normal[idx];
            n = ApplySwapYZDirIfEnabled(request.options.coordinate, n);
            v.normal = { static_cast<float>(n.x), static_cast<float>(n.y),
              static_cast<float>(n.z) };
          }

          if (has_uv) {
            const auto uv = mesh->vertex_uv[idx];
            v.texcoord = { static_cast<float>(uv.x), static_cast<float>(uv.y) };
          }

          const auto tangent_policy = request.options.tangent_policy;
          const bool preserve_authored_tangents
            = tangent_policy == GeometryAttributePolicy::kPreserveIfPresent
            || tangent_policy == GeometryAttributePolicy::kGenerateMissing;

          if (preserve_authored_tangents && mesh->vertex_tangent.exists
            && mesh->vertex_tangent.values.data != nullptr
            && mesh->vertex_tangent.indices.data != nullptr) {
            auto t = mesh->vertex_tangent[idx];
            t = ApplySwapYZDirIfEnabled(request.options.coordinate, t);
            v.tangent = { static_cast<float>(t.x), static_cast<float>(t.y),
              static_cast<float>(t.z) };
          }

          if (preserve_authored_tangents && mesh->vertex_bitangent.exists
            && mesh->vertex_bitangent.values.data != nullptr
            && mesh->vertex_bitangent.indices.data != nullptr) {
            auto b = mesh->vertex_bitangent[idx];
            b = ApplySwapYZDirIfEnabled(request.options.coordinate, b);
            v.bitangent = { static_cast<float>(b.x), static_cast<float>(b.y),
              static_cast<float>(b.z) };
          }

          if (mesh->vertex_color.exists
            && mesh->vertex_color.values.data != nullptr
            && mesh->vertex_color.indices.data != nullptr) {
            const auto c = mesh->vertex_color[idx];
            v.color = { static_cast<float>(c.x), static_cast<float>(c.y),
              static_cast<float>(c.z), static_cast<float>(c.w) };
          }

          vertices.push_back(v);

          bbox_min[0] = (std::min)(bbox_min[0], v.position.x);
          bbox_min[1] = (std::min)(bbox_min[1], v.position.y);
          bbox_min[2] = (std::min)(bbox_min[2], v.position.z);
          bbox_max[0] = (std::max)(bbox_max[0], v.position.x);
          bbox_max[1] = (std::max)(bbox_max[1], v.position.y);
          bbox_max[2] = (std::max)(bbox_max[2], v.position.z);
        }

        struct SubmeshBucket final {
          uint32_t scene_material_index = 0;
          AssetKey material_key {};
          std::vector<uint32_t> indices;
        };

        std::unordered_map<uint32_t, size_t> bucket_index_by_material;
        std::vector<SubmeshBucket> buckets;

        std::vector<uint32_t> tri_indices;
        tri_indices.resize(static_cast<size_t>(mesh->max_face_triangles) * 3);

        const auto default_material_key = (!effective_material_keys.empty())
          ? effective_material_keys.front()
          : AssetKey {};

        auto resolve_bucket = [&](const size_t face_i) -> SubmeshBucket& {
          uint32_t scene_material_index = 0;
          AssetKey material_key = default_material_key;

          if (mesh->face_material.data != nullptr
            && face_i < mesh->face_material.count && mesh->materials.data
            && mesh->materials.count > 0) {
            const uint32_t slot = mesh->face_material.data[face_i];
            if (slot != UFBX_NO_INDEX && slot < mesh->materials.count) {
              const auto* mat = mesh->materials.data[slot];
              if (mat != nullptr) {
                if (const auto it = scene_material_index_by_ptr.find(mat);
                  it != scene_material_index_by_ptr.end()) {
                  scene_material_index = it->second;
                }

                if (const auto it = material_key_by_ptr.find(mat);
                  it != material_key_by_ptr.end()) {
                  material_key = it->second;
                }
              }
            }
          }

          const auto found
            = bucket_index_by_material.find(scene_material_index);
          if (found != bucket_index_by_material.end()) {
            return buckets[found->second];
          }

          const auto bucket_i = buckets.size();
          bucket_index_by_material.emplace(scene_material_index, bucket_i);
          buckets.push_back(SubmeshBucket {
            .scene_material_index = scene_material_index,
            .material_key = material_key,
            .indices = {},
          });
          return buckets.back();
        };

        for (size_t face_i = 0; face_i < mesh->faces.count; ++face_i) {
          const auto face = mesh->faces.data[face_i];
          if (face.num_indices < 3) {
            continue;
          }

          auto& bucket = resolve_bucket(face_i);
          const auto tri_count = ufbx_triangulate_face(
            tri_indices.data(), tri_indices.size(), mesh, face);
          bucket.indices.insert(bucket.indices.end(), tri_indices.begin(),
            tri_indices.begin() + static_cast<ptrdiff_t>(tri_count) * 3);
        }

        buckets.erase(
          std::remove_if(buckets.begin(), buckets.end(),
            [](const SubmeshBucket& b) { return b.indices.empty(); }),
          buckets.end());

        std::sort(buckets.begin(), buckets.end(),
          [](const SubmeshBucket& a, const SubmeshBucket& b) {
            return a.scene_material_index < b.scene_material_index;
          });

        std::vector<uint32_t> indices;
        {
          size_t total = 0;
          for (const auto& b : buckets) {
            total += b.indices.size();
          }
          indices.reserve(total);
        }

        if (vertices.empty() || buckets.empty()) {
          ImportDiagnostic diag {
            .severity = ImportSeverity::kError,
            .code = "fbx.mesh.missing_buffers",
            .message = "FBX mesh does not produce valid vertex/index buffers",
            .source_path = request.source_path.string(),
            .object_path = std::string(mesh_name),
          };
          out.AddDiagnostic(std::move(diag));
          throw std::runtime_error("FBX mesh produced empty buffers");
        }

        // If tangents/bitangents were not authored, generate a consistent
        // per-vertex TBN basis from triangles (required for normal mapping).
        const auto tangent_policy = request.options.tangent_policy;
        const bool has_authored_tangents
          = mesh->vertex_tangent.exists && mesh->vertex_bitangent.exists;

        const bool should_generate_tangents
          = (tangent_policy == GeometryAttributePolicy::kGenerateMissing
              && !has_authored_tangents)
          || (tangent_policy == GeometryAttributePolicy::kAlwaysRecalculate);

        bool has_any_indices = false;
        for (const auto& b : buckets) {
          if (b.indices.size() >= 3) {
            has_any_indices = true;
            break;
          }
        }

        if (tangent_policy != GeometryAttributePolicy::kNone
          && should_generate_tangents && has_uv && has_any_indices) {
          std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0F));
          std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0F));

          for (const auto& bucket : buckets) {
            const auto tri_count = bucket.indices.size() / 3;
            for (size_t tri = 0; tri < tri_count; ++tri) {
              const auto i0 = bucket.indices[tri * 3 + 0];
              const auto i1 = bucket.indices[tri * 3 + 1];
              const auto i2 = bucket.indices[tri * 3 + 2];
              if (i0 >= vertices.size() || i1 >= vertices.size()
                || i2 >= vertices.size()) {
                continue;
              }

              const auto& v0 = vertices[i0];
              const auto& v1 = vertices[i1];
              const auto& v2 = vertices[i2];

              const glm::vec3 p0 = v0.position;
              const glm::vec3 p1 = v1.position;
              const glm::vec3 p2 = v2.position;

              const glm::vec2 w0 = v0.texcoord;
              const glm::vec2 w1 = v1.texcoord;
              const glm::vec2 w2 = v2.texcoord;

              const glm::vec3 e1 = p1 - p0;
              const glm::vec3 e2 = p2 - p0;
              const glm::vec2 d1 = w1 - w0;
              const glm::vec2 d2 = w2 - w0;

              const float denom = d1.x * d2.y - d1.y * d2.x;
              if (std::abs(denom) < 1e-8F) {
                continue;
              }
              const float r = 1.0F / denom;

              const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
              const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

              tan1[i0] += t;
              tan1[i1] += t;
              tan1[i2] += t;

              tan2[i0] += b;
              tan2[i1] += b;
              tan2[i2] += b;
            }
          }

          for (size_t vi = 0; vi < vertices.size(); ++vi) {
            auto n = vertices[vi].normal;
            const auto n_len = glm::length(n);
            if (n_len > 1e-8F) {
              n /= n_len;
            } else {
              n = glm::vec3(0.0F, 1.0F, 0.0F);
            }

            glm::vec3 t = tan1[vi];
            if (glm::length(t) < 1e-8F) {
              continue;
            }

            // Gram-Schmidt orthonormalization
            t = glm::normalize(t - n * glm::dot(n, t));

            glm::vec3 b = glm::cross(n, t);
            if (glm::dot(b, tan2[vi]) < 0.0F) {
              b = -b;
            }
            b = glm::normalize(b);

            vertices[vi].normal = n;
            vertices[vi].tangent = t;
            vertices[vi].bitangent = b;
          }
        }

        // --- Emit buffer resources (vertex + index) ---
        const auto vb_bytes = std::as_bytes(std::span(vertices));
        constexpr uint32_t vb_stride = sizeof(Vertex);

        const auto vb_usage_flags
          = static_cast<uint32_t>(BufferResource::UsageFlags::kVertexBuffer)
          | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);
        const auto vb_index
          = GetOrCreateBufferIndex(vb_bytes, vb_stride, vb_usage_flags,
            vb_stride, static_cast<uint8_t>(oxygen::Format::kUnknown));

        std::vector<SubMeshDesc> submeshes;
        submeshes.reserve(buckets.size());
        std::vector<MeshViewDesc> views;
        views.reserve(buckets.size());

        MeshViewDesc::BufferIndexT index_cursor = 0;
        for (const auto& bucket : buckets) {
          float sm_bbox_min[3] = {
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
          };
          float sm_bbox_max[3] = {
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
          };

          for (const auto vi : bucket.indices) {
            if (vi >= vertices.size()) {
              continue;
            }
            const auto& v = vertices[vi];
            sm_bbox_min[0] = (std::min)(sm_bbox_min[0], v.position.x);
            sm_bbox_min[1] = (std::min)(sm_bbox_min[1], v.position.y);
            sm_bbox_min[2] = (std::min)(sm_bbox_min[2], v.position.z);
            sm_bbox_max[0] = (std::max)(sm_bbox_max[0], v.position.x);
            sm_bbox_max[1] = (std::max)(sm_bbox_max[1], v.position.y);
            sm_bbox_max[2] = (std::max)(sm_bbox_max[2], v.position.z);
          }

          const auto name
            = "mat_" + std::to_string(bucket.scene_material_index);

          SubMeshDesc sm {};
          TruncateAndNullTerminate(sm.name, std::size(sm.name), name);
          sm.material_asset_key = bucket.material_key;
          sm.mesh_view_count = 1;
          std::copy_n(sm_bbox_min, 3, sm.bounding_box_min);
          std::copy_n(sm_bbox_max, 3, sm.bounding_box_max);
          submeshes.push_back(sm);

          const auto first_index = index_cursor;
          const auto index_count
            = static_cast<MeshViewDesc::BufferIndexT>(bucket.indices.size());
          index_cursor += index_count;

          views.push_back(MeshViewDesc {
            .first_index = first_index,
            .index_count = index_count,
            .first_vertex = 0,
            .vertex_count
            = static_cast<MeshViewDesc::BufferIndexT>(vertices.size()),
          });

          indices.insert(
            indices.end(), bucket.indices.begin(), bucket.indices.end());
        }

        const auto ib_bytes = std::as_bytes(std::span(indices));

        const auto ib_usage_flags
          = static_cast<uint32_t>(BufferResource::UsageFlags::kIndexBuffer)
          | static_cast<uint32_t>(BufferResource::UsageFlags::kStatic);
        const auto ib_index
          = GetOrCreateBufferIndex(ib_bytes, alignof(uint32_t), ib_usage_flags,
            0, static_cast<uint8_t>(oxygen::Format::kR32UInt));

        // --- Emit geometry asset descriptor (desc + mesh + submesh + view) ---
        const auto storage_mesh_name
          = NamespaceImportedAssetName(request, mesh_name);

        const auto geo_virtual_path
          = request.loose_cooked_layout.GeometryVirtualPath(storage_mesh_name);
        const auto geo_relpath
          = request.loose_cooked_layout.DescriptorDirFor(AssetType::kGeometry)
          + "/"
          + LooseCookedLayout::GeometryDescriptorFileName(storage_mesh_name);

        AssetKey geo_key {};
        switch (request.options.asset_key_policy) {
        case AssetKeyPolicy::kDeterministicFromVirtualPath:
          geo_key = MakeDeterministicAssetKey(geo_virtual_path);
          break;
        case AssetKeyPolicy::kRandom:
          geo_key = MakeRandomAssetKey();
          break;
        }

        GeometryAssetDesc geo_desc {};
        geo_desc.header.asset_type = static_cast<uint8_t>(AssetType::kGeometry);
        TruncateAndNullTerminate(
          geo_desc.header.name, std::size(geo_desc.header.name), mesh_name);
        geo_desc.lod_count = 1;
        std::copy_n(bbox_min, 3, geo_desc.bounding_box_min);
        std::copy_n(bbox_max, 3, geo_desc.bounding_box_max);

        MeshDesc lod0 {};
        TruncateAndNullTerminate(lod0.name, std::size(lod0.name), mesh_name);
        lod0.mesh_type = static_cast<uint8_t>(MeshType::kStandard);
        lod0.submesh_count = static_cast<uint32_t>(submeshes.size());
        lod0.mesh_view_count = static_cast<uint32_t>(views.size());
        lod0.info.standard.vertex_buffer = vb_index;
        lod0.info.standard.index_buffer = ib_index;
        std::copy_n(bbox_min, 3, lod0.info.standard.bounding_box_min);
        std::copy_n(bbox_max, 3, lod0.info.standard.bounding_box_max);

        oxygen::serio::MemoryStream desc_stream;
        oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(desc_stream);
        const auto pack = writer.ScopedAlignment(1);
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const GeometryAssetDesc, 1>(&geo_desc, 1)));
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const MeshDesc, 1>(&lod0, 1)));
        for (size_t sm_i = 0; sm_i < submeshes.size(); ++sm_i) {
          const auto& sm = submeshes[sm_i];
          const auto& view = views[sm_i];
          (void)writer.WriteBlob(
            std::as_bytes(std::span<const SubMeshDesc, 1>(&sm, 1)));
          (void)writer.WriteBlob(
            std::as_bytes(std::span<const MeshViewDesc, 1>(&view, 1)));
        }

        const auto geo_bytes = desc_stream.Data();

        LOG_F(INFO,
          "Emit geometry {} '{}' -> {} (vb={}, ib={}, vtx={}, idx={})",
          written_geometry, mesh_name.c_str(), geo_relpath.c_str(), vb_index,
          ib_index, vertices.size(), indices.size());

        out.WriteAssetDescriptor(geo_key, AssetType::kGeometry,
          geo_virtual_path, geo_relpath, geo_bytes);

        out_geometry.push_back(ImportedGeometry {
          .mesh = mesh,
          .key = geo_key,
        });

        written_geometry += 1;
      }

      if (buffers_table.empty()) {
        return;
      }

      oxygen::serio::MemoryStream table_stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> table_writer(
        table_stream);
      const auto pack = table_writer.ScopedAlignment(1);
      (void)table_writer.WriteBlob(
        std::as_bytes(std::span<const BufferResourceDesc>(buffers_table)));

      out.WriteFile(FileKind::kBuffersTable,
        request.loose_cooked_layout.BuffersTableRelPath(), table_stream.Data());

      out.WriteFile(FileKind::kBuffersData,
        request.loose_cooked_layout.BuffersDataRelPath(),
        std::span<const std::byte>(buffers_data.data(), buffers_data.size()));
    }

    static auto WriteScene_(const ufbx_scene& scene,
      const ImportRequest& request, CookedContentWriter& out,
      const std::vector<ImportedGeometry>& geometry, uint32_t& written_scenes)
      -> void
    {
      using oxygen::data::AssetType;
      using oxygen::data::ComponentType;
      using oxygen::data::pak::NodeRecord;
      using oxygen::data::pak::OrthographicCameraRecord;
      using oxygen::data::pak::PerspectiveCameraRecord;
      using oxygen::data::pak::RenderableRecord;
      using oxygen::data::pak::SceneAssetDesc;
      using oxygen::data::pak::SceneComponentTableDesc;

      const auto scene_name = BuildSceneName(request);
      const auto virtual_path
        = request.loose_cooked_layout.SceneVirtualPath(scene_name);
      const auto relpath
        = request.loose_cooked_layout.SceneDescriptorRelPath(scene_name);

      AssetKey scene_key {};
      switch (request.options.asset_key_policy) {
      case AssetKeyPolicy::kDeterministicFromVirtualPath:
        scene_key = MakeDeterministicAssetKey(virtual_path);
        break;
      case AssetKeyPolicy::kRandom:
        scene_key = MakeRandomAssetKey();
        break;
      }

      std::vector<NodeRecord> nodes;
      nodes.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<std::byte> strings;
      strings.push_back(std::byte { 0 });

      std::vector<RenderableRecord> renderables;
      renderables.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<PerspectiveCameraRecord> perspective_cameras;
      perspective_cameras.reserve(static_cast<size_t>(scene.nodes.count));

      std::vector<OrthographicCameraRecord> orthographic_cameras;
      orthographic_cameras.reserve(static_cast<size_t>(scene.nodes.count));

      size_t camera_attr_total = 0;
      size_t camera_attr_skipped = 0;

      struct NodeRef final {
        const ufbx_node* node = nullptr;
        uint32_t index = 0;
        std::string name;
      };
      std::vector<NodeRef> node_refs;
      node_refs.reserve(static_cast<size_t>(scene.nodes.count));

      auto FindGeometryKey
        = [&](const ufbx_mesh* mesh) -> std::optional<AssetKey> {
        if (mesh == nullptr) {
          return std::nullopt;
        }
        for (const auto& g : geometry) {
          if (g.mesh == mesh) {
            return g.key;
          }
        }
        return std::nullopt;
      };

      auto AppendString =
        [&](const std::string_view s) -> oxygen::data::pak::StringTableOffsetT {
        const auto offset
          = static_cast<oxygen::data::pak::StringTableOffsetT>(strings.size());
        const auto bytes
          = std::as_bytes(std::span<const char>(s.data(), s.size()));
        strings.insert(strings.end(), bytes.begin(), bytes.end());
        strings.push_back(std::byte { 0 });
        return offset;
      };

      auto MakeNodeKey
        = [&](const std::string_view node_virtual_path) -> AssetKey {
        return MakeDeterministicAssetKey(node_virtual_path);
      };

      auto Traverse
        = [&](auto&& self, const ufbx_node* n, uint32_t parent_index,
            std::string_view parent_name, uint32_t& ordinal) -> void {
        if (n == nullptr) {
          return;
        }

        const auto authored_name = ToStringView(n->name);
        auto name
          = BuildSceneNodeName(authored_name, request, ordinal, parent_name);

        NodeRecord rec {};
        const auto node_virtual_path = virtual_path + "/" + std::string(name);
        rec.node_id = MakeNodeKey(node_virtual_path);
        rec.scene_name_offset = AppendString(name);
        rec.parent_index = parent_index;
        rec.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;

        // Use ufbx's post-conversion local TRS directly.
        //
        // Rationale: When `opts.target_axes` / `opts.target_unit_meters` is
        // set, ufbx computes a consistent local TRS for each node in the target
        // coordinate system. Reconstructing TRS from matrices (whether via
        // generic decomposition or matrix-to-TRS) can re-introduce sign/
        // reflection ambiguity and lead to flips.
        const ufbx_transform local_trs = ApplySwapYZIfEnabled(
          request.options.coordinate, n->local_transform);

        LOG_F(INFO,
          "Node '{}' (ordinal={}) local_trs: T=({:.3f}, {:.3f}, {:.3f}) "
          "R=({:.3f}, {:.3f}, {:.3f}, {:.3f}) S=({:.3f}, {:.3f}, {:.3f})",
          name, ordinal, local_trs.translation.x, local_trs.translation.y,
          local_trs.translation.z, local_trs.rotation.x, local_trs.rotation.y,
          local_trs.rotation.z, local_trs.rotation.w, local_trs.scale.x,
          local_trs.scale.y, local_trs.scale.z);

        rec.translation[0] = static_cast<float>(local_trs.translation.x);
        rec.translation[1] = static_cast<float>(local_trs.translation.y);
        rec.translation[2] = static_cast<float>(local_trs.translation.z);

        // Store quaternion as x,y,z,w in NodeRecord.
        rec.rotation[0] = static_cast<float>(local_trs.rotation.x);
        rec.rotation[1] = static_cast<float>(local_trs.rotation.y);
        rec.rotation[2] = static_cast<float>(local_trs.rotation.z);
        rec.rotation[3] = static_cast<float>(local_trs.rotation.w);

        rec.scale[0] = static_cast<float>(local_trs.scale.x);
        rec.scale[1] = static_cast<float>(local_trs.scale.y);
        rec.scale[2] = static_cast<float>(local_trs.scale.z);

        const auto index = static_cast<uint32_t>(nodes.size());
        if (index == 0) {
          rec.parent_index = 0;
        }

        nodes.push_back(rec);
        node_refs.push_back(NodeRef {
          .node = n,
          .index = index,
          .name = name,
        });

        if (const auto geo_key = FindGeometryKey(n->mesh);
          geo_key.has_value()) {
          renderables.push_back(RenderableRecord {
            .node_index = index,
            .geometry_key = *geo_key,
            .visible = 1,
            .reserved = {},
          });
        }

        if (n->camera != nullptr) {
          const auto* cam = n->camera;
          ++camera_attr_total;
          if (cam->projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE) {
            float near_plane = std::abs(static_cast<float>(cam->near_plane));
            float far_plane = std::abs(static_cast<float>(cam->far_plane));
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            const float fov_y_rad = static_cast<float>(cam->field_of_view_deg.y)
              * (std::numbers::pi_v<float> / 180.0f);
            perspective_cameras.push_back(PerspectiveCameraRecord {
              .node_index = index,
              .fov_y = fov_y_rad,
              .aspect_ratio = static_cast<float>(cam->aspect_ratio),
              .near_plane = near_plane,
              .far_plane = far_plane,
              .reserved = {},
            });
          } else if (cam->projection_mode
            == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) {
            float near_plane = std::abs(static_cast<float>(cam->near_plane));
            float far_plane = std::abs(static_cast<float>(cam->far_plane));
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            const float half_w
              = static_cast<float>(cam->orthographic_size.x) * 0.5f;
            const float half_h
              = static_cast<float>(cam->orthographic_size.y) * 0.5f;
            orthographic_cameras.push_back(OrthographicCameraRecord {
              .node_index = index,
              .left = -half_w,
              .right = half_w,
              .bottom = -half_h,
              .top = half_h,
              .near_plane = near_plane,
              .far_plane = far_plane,
              .reserved = {},
            });
          } else {
            ++camera_attr_skipped;
            LOG_F(INFO,
              "Scene camera attribute skipped: node_index={} name='{}' "
              "projection_mode={}",
              index, name.c_str(), static_cast<int>(cam->projection_mode));
          }
        }

        ++ordinal;

        for (size_t i = 0; i < n->children.count; ++i) {
          const auto* child = n->children.data[i];
          self(self, child, index, name, ordinal);
        }
      };

      uint32_t ordinal = 0;
      Traverse(Traverse, scene.root_node, 0, {}, ordinal);

      std::sort(renderables.begin(), renderables.end(),
        [](const RenderableRecord& a, const RenderableRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(perspective_cameras.begin(), perspective_cameras.end(),
        [](const PerspectiveCameraRecord& a, const PerspectiveCameraRecord& b) {
          return a.node_index < b.node_index;
        });
      std::sort(orthographic_cameras.begin(), orthographic_cameras.end(),
        [](const OrthographicCameraRecord& a,
          const OrthographicCameraRecord& b) {
          return a.node_index < b.node_index;
        });

      LOG_F(INFO,
        "Scene cameras: camera_attrs={} skipped_attrs={} perspective={} "
        "ortho={}",
        camera_attr_total, camera_attr_skipped, perspective_cameras.size(),
        orthographic_cameras.size());
      for (const auto& cam : perspective_cameras) {
        const auto name = (cam.node_index < node_refs.size())
          ? node_refs[cam.node_index].name
          : std::string("<invalid>");
        const float fov_y_deg
          = cam.fov_y * (180.0f / std::numbers::pi_v<float>);
        LOG_F(INFO,
          "  PerspectiveCamera node_index={} name='{}' fov_y_deg={} near={} "
          "far={} aspect={}",
          cam.node_index, name.c_str(), fov_y_deg, cam.near_plane,
          cam.far_plane, cam.aspect_ratio);
      }
      for (const auto& cam : orthographic_cameras) {
        const auto name = (cam.node_index < node_refs.size())
          ? node_refs[cam.node_index].name
          : std::string("<invalid>");
        LOG_F(INFO,
          "  OrthographicCamera node_index={} name='{}' l={} r={} b={} t={} "
          "near={} far={}",
          cam.node_index, name.c_str(), cam.left, cam.right, cam.bottom,
          cam.top, cam.near_plane, cam.far_plane);
      }

      if (nodes.empty()) {
        NodeRecord root {};
        const auto root_name = std::string("root");
        root.node_id = MakeNodeKey(virtual_path + "/" + root_name);
        root.scene_name_offset = AppendString(root_name);
        root.parent_index = 0;
        root.node_flags = oxygen::data::pak::kSceneNodeFlag_Visible;
        nodes.push_back(root);
      }

      oxygen::serio::MemoryStream stream;
      oxygen::serio::Writer<oxygen::serio::MemoryStream> writer(stream);
      const auto packed = writer.ScopedAlignment(1);

      SceneAssetDesc desc {};
      desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
      TruncateAndNullTerminate(
        desc.header.name, sizeof(desc.header.name), scene_name);
      desc.header.version = 1;

      desc.nodes.offset = sizeof(SceneAssetDesc);
      desc.nodes.count = static_cast<uint32_t>(nodes.size());
      desc.nodes.entry_size = sizeof(NodeRecord);

      const auto nodes_bytes
        = std::as_bytes(std::span<const NodeRecord>(nodes));

      desc.scene_strings.offset
        = static_cast<oxygen::data::pak::StringTableOffsetT>(
          sizeof(SceneAssetDesc) + nodes_bytes.size());
      desc.scene_strings.size
        = static_cast<oxygen::data::pak::StringTableSizeT>(strings.size());

      const auto strings_bytes = std::span<const std::byte>(strings);

      std::vector<SceneComponentTableDesc> component_dir;
      component_dir.reserve(3);

      auto table_cursor = static_cast<oxygen::data::pak::OffsetT>(
        sizeof(SceneAssetDesc) + nodes_bytes.size() + strings_bytes.size());

      if (!renderables.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type = static_cast<uint32_t>(ComponentType::kRenderable),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(renderables.size()),
            .entry_size = sizeof(RenderableRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          renderables.size() * sizeof(RenderableRecord));
      }

      if (!perspective_cameras.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type
          = static_cast<uint32_t>(ComponentType::kPerspectiveCamera),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(perspective_cameras.size()),
            .entry_size = sizeof(PerspectiveCameraRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          perspective_cameras.size() * sizeof(PerspectiveCameraRecord));
      }

      if (!orthographic_cameras.empty()) {
        component_dir.push_back(SceneComponentTableDesc {
          .component_type
          = static_cast<uint32_t>(ComponentType::kOrthographicCamera),
          .table = {
            .offset = table_cursor,
            .count = static_cast<uint32_t>(orthographic_cameras.size()),
            .entry_size = sizeof(OrthographicCameraRecord),
          },
        });
        table_cursor += static_cast<oxygen::data::pak::OffsetT>(
          orthographic_cameras.size() * sizeof(OrthographicCameraRecord));
      }

      desc.component_table_directory_offset = table_cursor;
      desc.component_table_count = static_cast<uint32_t>(component_dir.size());

      (void)writer.WriteBlob(
        std::as_bytes(std::span<const SceneAssetDesc, 1>(&desc, 1)));
      (void)writer.WriteBlob(nodes_bytes);
      (void)writer.WriteBlob(strings_bytes);
      if (!renderables.empty()) {
        (void)writer.WriteBlob(
          std::as_bytes(std::span<const RenderableRecord>(renderables)));
      }
      if (!perspective_cameras.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const PerspectiveCameraRecord>(perspective_cameras)));
      }
      if (!orthographic_cameras.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const OrthographicCameraRecord>(orthographic_cameras)));
      }
      if (!component_dir.empty()) {
        (void)writer.WriteBlob(std::as_bytes(
          std::span<const SceneComponentTableDesc>(component_dir)));
      }

      const auto bytes = stream.Data();

      LOG_F(INFO, "Emit scene '{}' -> {} (nodes={}, renderables={})",
        scene_name.c_str(), relpath.c_str(), nodes.size(), renderables.size());

      out.WriteAssetDescriptor(
        scene_key, AssetType::kScene, virtual_path, relpath, bytes);
      written_scenes += 1;
    }
  };

} // namespace

auto CreateFbxImporter() -> std::unique_ptr<Importer>
{
  return std::make_unique<FbxImporter>();
}

} // namespace oxygen::content::import
