//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/TextureEmitter.h>

#include <algorithm>
#include <cctype>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>
#include <Oxygen/Content/Import/util/Constants.h>
#include <Oxygen/Content/Import/util/Signature.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import::emit {

namespace {

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return std::string_view(s.data, s.length);
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

  // Index 0 is reserved: a 1x1 white RGBA8 placeholder texture.
  // Use the cooker to create it with proper packing.
  CookerConfig config {};
  auto fallback = CreateFallbackTexture(config);

  const auto data_offset = AppendResource(state.appender,
    std::span<const std::byte>(
      fallback.payload.data(), fallback.payload.size()),
    util::kRowPitchAlignment);

  auto desc = fallback.desc;
  desc.data_offset = data_offset;

  state.table.push_back(desc);
}

auto GetOrCreateTextureResourceIndexWithCooker(const ImportRequest& request,
  CookedContentWriter& cooked_out, TextureEmissionState& state,
  const ufbx_texture* texture, const CookerConfig& config) -> uint32_t
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
  const bool is_embedded = (texture != nullptr
    && texture->content.data != nullptr && texture->content.size > 0);

  std::string texture_id;
  std::filesystem::path resolved;
  std::span<const std::byte> source_bytes;
  std::vector<std::byte> file_bytes;

  if (is_embedded) {
    source_bytes = std::span<const std::byte>(
      reinterpret_cast<const std::byte*>(texture->content.data),
      texture->content.size);
    texture_id = "embedded:"
      + util::Sha256ToHex(oxygen::base::ComputeSha256(source_bytes));
    if (const auto it = state.index_by_texture_id.find(texture_id);
      it != state.index_by_texture_id.end()) {
      state.index_by_file_texture.insert_or_assign(file_tex, it->second);
      return it->second;
    }
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

    // Read file bytes for cooker
    if (!resolved.empty()) {
      auto opt_bytes = TryReadWholeFileBytes(resolved);
      if (opt_bytes.has_value()) {
        file_bytes = std::move(opt_bytes.value());
        source_bytes
          = std::span<const std::byte>(file_bytes.data(), file_bytes.size());
      }
    }
  }

  // Cook texture with fallback to placeholder
  auto cooked = CookTextureWithFallback(source_bytes, config, texture_id);

  if (cooked.is_placeholder) {
    if (!resolved.empty()) {
      LOG_F(WARNING,
        "FBX import: failed to load texture '{}' (embedded={}, path='{}'): "
        "using 1x1 placeholder",
        std::string(id).c_str(), is_embedded,
        resolved.generic_string().c_str());
    } else {
      LOG_F(WARNING,
        "FBX import: failed to load texture '{}' (embedded={}): "
        "using 1x1 placeholder",
        std::string(id).c_str(), is_embedded);
    }

    ImportDiagnostic diag {
      .severity = ImportSeverity::kWarning,
      .code = "fbx.texture_decode_failed",
      .message = "failed to decode texture '" + std::string(id)
        + "'; using 1x1 placeholder",
      .source_path = request.source_path.string(),
      .object_path = std::string(id),
    };
    cooked_out.AddDiagnostic(std::move(diag));
  }

  // Build signature using stored hash
  const auto signature = util::MakeTextureSignatureFromStoredHash(cooked.desc);

  // Check for duplicate by signature
  if (const auto it = state.index_by_signature.find(signature);
    it != state.index_by_signature.end()) {
    const auto existing_index = it->second;
    state.index_by_file_texture.insert_or_assign(file_tex, existing_index);
    if (!texture_id.empty()) {
      state.index_by_texture_id.insert_or_assign(texture_id, existing_index);
    }
    LOG_F(INFO,
      "Reuse texture '{}' ({}x{}, mips={}, format={}, embedded={}, "
      "placeholder={}) -> index {}",
      std::string(id).c_str(), cooked.desc.width, cooked.desc.height,
      cooked.desc.mip_levels, cooked.desc.format, is_embedded,
      cooked.is_placeholder, existing_index);
    return existing_index;
  }

  // Append new texture to data file
  const auto data_offset = AppendResource(state.appender,
    std::span<const std::byte>(cooked.payload.data(), cooked.payload.size()),
    util::kRowPitchAlignment);

  // Update descriptor with data offset
  auto desc = cooked.desc;
  desc.data_offset = data_offset;

  LOG_F(INFO,
    "Emit texture '{}' ({}x{}, mips={}, format={}, bytes={}, embedded={}, "
    "placeholder={}) -> index {}",
    std::string(id).c_str(), desc.width, desc.height, desc.mip_levels,
    desc.format, cooked.payload.size(), is_embedded, cooked.is_placeholder,
    state.table.size());

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
