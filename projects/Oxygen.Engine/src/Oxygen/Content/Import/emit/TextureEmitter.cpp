//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/TextureEmitter.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/util/Constants.h>
#include <Oxygen/Content/Import/util/Signature.h>
#include <Oxygen/Content/Import/util/TextureRepack.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::emit {

namespace {

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return std::string_view(s.data, s.length);
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

} // namespace

auto ResolveFileTexture(const ufbx_texture* texture) -> const ufbx_texture*
{
  if (texture == nullptr) {
    return nullptr;
  }

  if (texture->file_textures.count > 0) {
    return texture->file_textures.data[0];
  }

  return texture;
}

auto TextureIdString(const ufbx_texture& texture) -> std::string_view
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

auto NormalizeTexturePathId(std::filesystem::path p) -> std::string
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

auto SelectBaseColorTexture(const ufbx_material& material)
  -> const ufbx_texture*
{
  const auto& pbr = material.pbr.base_color;
  if (!pbr.feature_disabled && pbr.texture != nullptr) {
    return pbr.texture;
  }
  const auto& fbx = material.fbx.diffuse_color;
  if (!fbx.feature_disabled && fbx.texture != nullptr) {
    return fbx.texture;
  }
  return nullptr;
}

auto SelectNormalTexture(const ufbx_material& material) -> const ufbx_texture*
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

auto SelectMetallicTexture(const ufbx_material& material) -> const ufbx_texture*
{
  const auto& pbr = material.pbr.metalness;
  if (!pbr.feature_disabled && pbr.texture != nullptr) {
    return pbr.texture;
  }
  return nullptr;
}

auto SelectRoughnessTexture(const ufbx_material& material)
  -> const ufbx_texture*
{
  const auto& pbr = material.pbr.roughness;
  if (!pbr.feature_disabled && pbr.texture != nullptr) {
    return pbr.texture;
  }
  return nullptr;
}

auto SelectAmbientOcclusionTexture(const ufbx_material& material)
  -> const ufbx_texture*
{
  const auto& pbr = material.pbr.ambient_occlusion;
  if (!pbr.feature_disabled && pbr.texture != nullptr) {
    return pbr.texture;
  }
  return nullptr;
}

auto SelectEmissiveTexture(const ufbx_material& material) -> const ufbx_texture*
{
  const auto& pbr = material.pbr.emission_color;
  if (!pbr.feature_disabled && pbr.texture != nullptr) {
    return pbr.texture;
  }
  const auto& fbx = material.fbx.emission_color;
  if (!fbx.feature_disabled && fbx.texture != nullptr) {
    return fbx.texture;
  }
  return nullptr;
}

auto EnsureFallbackTexture(TextureEmissionState& state) -> void
{
  if (!state.table.empty()) {
    return;
  }

  using oxygen::data::pak::TextureResourceDesc;

  // Index 0 is reserved and must exist.
  // Use a 1x1 white RGBA8, packed with a 256-byte row pitch.
  const std::array<std::byte, 4> white = { std::byte { 0xFF },
    std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF } };

  const auto packed = util::RepackRgba8ToRowPitchAligned(
    std::span<const std::byte>(white.data(), white.size()), 1, 1,
    util::kRowPitchAlignment);

  const auto content_hash = util::ComputeContentHash(
    std::span<const std::byte>(packed.data(), packed.size()));

  const auto data_offset = AppendResource(state.appender,
    std::span<const std::byte>(packed.data(), packed.size()),
    util::kRowPitchAlignment);

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
  desc.alignment = static_cast<uint32_t>(util::kRowPitchAlignment);
  desc.content_hash = content_hash;

  state.table.push_back(desc);
}

auto GetOrCreateTextureResourceIndex(const ImportRequest& request,
  CookedContentWriter& cooked_out, TextureEmissionState& state,
  const ufbx_texture* texture) -> uint32_t
{
  const auto* file_tex = ResolveFileTexture(texture);
  if (file_tex == nullptr) {
    return 0;
  }

  EnsureFallbackTexture(state);

  // Check if already processed
  if (const auto it = state.index_by_file_texture.find(file_tex);
    it != state.index_by_file_texture.end()) {
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
      = "embedded:" + util::Sha256ToHex(oxygen::base::ComputeSha256(bytes));
    if (const auto it = state.index_by_texture_id.find(texture_id);
      it != state.index_by_texture_id.end()) {
      state.index_by_file_texture.insert_or_assign(file_tex, it->second);
      return it->second;
    }
    decoded = DecodeImageRgba8FromMemory(bytes);
  } else {
    auto rel = ToStringView(file_tex->relative_filename);
    auto abs = ToStringView(file_tex->filename);

    if (rel.empty() && abs.empty()) {
      const auto rel_prop
        = ufbx_find_string(&file_tex->props, "RelativeFilename", {});
      const auto abs_prop = ufbx_find_string(&file_tex->props, "FileName", {});
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
        const auto abs_prop = ufbx_find_string(&texture->props, "FileName", {});
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

    texture_id
      = !resolved.empty() ? NormalizeTexturePathId(resolved) : std::string(id);

    if (!texture_id.empty()) {
      if (const auto it = state.index_by_texture_id.find(texture_id);
        it != state.index_by_texture_id.end()) {
        state.index_by_file_texture.insert_or_assign(file_tex, it->second);
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
      if (!resolved.empty()) {
        LOG_F(WARNING,
          "FBX import: failed to load texture '{}' (embedded={}, path='{}'): "
          "{}; using 1x1 placeholder",
          std::string(id).c_str(), is_embedded,
          resolved.generic_string().c_str(), decoded.error.c_str());
      } else {
        LOG_F(WARNING,
          "FBX import: failed to load texture '{}' (embedded={}): {} "
          "using 1x1 placeholder",
          std::string(id).c_str(), is_embedded, decoded.error.c_str());
      }

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

  const auto packed_pixels = util::RepackRgba8ToRowPitchAligned(
    pixels, width, height, util::kRowPitchAlignment);

  // Compute content hash before building descriptor
  const auto content_hash = util::ComputeContentHash(
    std::span<const std::byte>(packed_pixels.data(), packed_pixels.size()));

  TextureEmissionState::TextureResourceDesc desc {};
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
  desc.alignment = static_cast<uint32_t>(util::kRowPitchAlignment);
  desc.content_hash = content_hash;

  // Build signature using stored hash
  const auto signature = util::MakeTextureSignatureFromStoredHash(desc);

  // Check for duplicate by signature
  if (const auto it = state.index_by_signature.find(signature);
    it != state.index_by_signature.end()) {
    const auto existing_index = it->second;
    state.index_by_file_texture.insert_or_assign(file_tex, existing_index);
    if (!texture_id.empty()) {
      state.index_by_texture_id.insert_or_assign(texture_id, existing_index);
    }
    LOG_F(INFO,
      "Reuse texture '{}' ({}x{}, bytes={}, embedded={}, placeholder={}) -> "
      "index {}",
      std::string(id).c_str(), width, height, pixels.size(), is_embedded,
      used_placeholder, existing_index);
    return existing_index;
  }

  // Append new texture to data file
  const auto data_offset = AppendResource(state.appender,
    std::span<const std::byte>(packed_pixels.data(), packed_pixels.size()),
    util::kRowPitchAlignment);

  desc.data_offset = data_offset;

  LOG_F(INFO,
    "Emit texture '{}' ({}x{}, bytes={}, embedded={}, placeholder={}) -> "
    "index {}",
    std::string(id).c_str(), width, height, pixels.size(), is_embedded,
    used_placeholder, state.table.size());

  const auto index = static_cast<uint32_t>(state.table.size());
  state.table.push_back(desc);
  state.index_by_file_texture.insert_or_assign(file_tex, index);
  if (!texture_id.empty()) {
    state.index_by_texture_id.insert_or_assign(texture_id, index);
  }
  state.index_by_signature.emplace(signature, index);
  return index;
}

} // namespace oxygen::content::import::emit
