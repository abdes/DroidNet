//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::content::import::adapters {

//! Where a model's external texture is looked for, in order, when its
//! authored path is missing: the file of that name beside the model and in its
//! `textures` and `Textures` folders, then the same in the model folder's
//! parent, as model-sharing sites lay them out (`Models/` beside `Textures/`).
//! The authored path and paths naming the same file are never listed twice.
[[nodiscard]] inline auto TextureFallbackPaths(
  const std::filesystem::path& model, const std::filesystem::path& texture)
  -> std::vector<std::filesystem::path>
{
  auto paths = std::vector<std::filesystem::path> {};
  if (!texture.has_filename()) {
    return paths;
  }
  auto seen = std::vector<std::string> { base::PathIdentityKey(texture) };
  const auto folder = model.parent_path();
  const auto parent = folder.parent_path();
  for (const auto& candidate :
    { folder, folder / "textures", folder / "Textures", parent,
      parent / "textures", parent / "Textures" }) {
    auto path = (candidate / texture.filename()).lexically_normal();
    auto key = base::PathIdentityKey(path);
    if (std::ranges::find(seen, key) == seen.end()) {
      seen.push_back(std::move(key));
      paths.push_back(std::move(path));
    }
  }
  return paths;
}

//! One texture use, before reading its external file or decoding embedded
//! bytes.
struct ModelTextureSource final {
  std::string source_id;
  std::string external_texture_id;
  //! The image the texture cooks from; its usages share one name.
  std::string image_key;
  //! The texture's or image's authored name, or the image file's stem.
  std::string authored_name;
  //! The image's index in the source, for the `texture_<index>` fallback.
  uint32_t ordinal = 0;
  //! How the material uses the image (`base_color`).
  std::string usage;
  //! The product name, `<image name>.<usage>`, set by `NameModelTextures`.
  std::string name;
  const void* source_key = nullptr;
  std::filesystem::path source_path;
  bool embedded = false;
  //! The source declares its normal maps green-up (glTF, and FBX files from
  //! green-up applications); the cook turns them green-down
  //! (`space::texture`).
  bool normal_maps_green_up = false;
  TextureImportDesc desc;
  std::string packing_policy_id;
  TexturePipeline::OutputFormatPolicy output_format_policy
    = TexturePipeline::OutputFormatPolicy::kMaterialPreset;
  TexturePipeline::FailurePolicy failure_policy
    = TexturePipeline::FailurePolicy::kStrict;
};

struct ModelTexturePreparation final {
  std::vector<ModelTextureSource> sources;
  std::vector<ImportDiagnostic> diagnostics;
  bool success = true;
};

//! Names a model's textures: one name per image, then its usage.
/*!
 Image names are registered in the order of the sources, which are sorted by
 source key, so analysis and production assign the same names: `a/diffuse.png`
 and `b/diffuse.png` become `diffuse` and `diffuse_1`, and one image used as
 base color and as normal map gives `Paint.base_color` and `Paint.normal`.
*/
inline auto NameModelTextures(std::span<ModelTextureSource> sources) -> void
{
  auto naming = NamingService {};
  auto names = std::map<std::string, std::string, std::less<>> {};
  for (auto& source : sources) {
    auto [it, added] = names.try_emplace(source.image_key);
    if (added) {
      it->second = naming.MakeUniqueName(source.authored_name,
        NamingContext {
          .kind = ImportNameKind::kTexture,
          .ordinal = source.ordinal,
        });
    }
    source.name = it->second + "." + source.usage;
  }
}

//! Applies the import's texture settings to named textures.
/*!
 A texture takes the override its name keys in `texture_overrides`, else the
 job's settings. The maximum size and the green flip of normal maps apply
 whether or not the settings enable the other cooking choices; color textures
 (albedo and emissive) take the color output format, the others the data one.
 A normal map is flipped to the engine's green-down convention when its source
 declares it green-up, and `flip_normal_green` reverses that for a map that
 departs from its source's convention.
*/
inline auto TuneModelTextures(
  std::span<ModelTextureSource> sources, const ImportOptions& options) -> void
{
  for (auto& source : sources) {
    const auto it = options.texture_overrides.find(source.name);
    const auto& tuning = it != options.texture_overrides.end()
      ? it->second
      : options.texture_tuning;
    auto& desc = source.desc;
    desc.max_size = tuning.max_size;
    if (desc.intent == TextureIntent::kNormalTS) {
      desc.flip_normal_green
        = source.normal_maps_green_up != tuning.flip_normal_green;
    }
    if (tuning.enabled) {
      desc.flip_y_on_decode = tuning.flip_y_on_decode;
      desc.force_rgba_on_decode = tuning.force_rgba_on_decode;
      desc.mip_policy = tuning.mip_policy;
      desc.max_mip_levels = tuning.max_mip_levels;
      desc.mip_filter = tuning.mip_filter;
      desc.output_format = desc.intent == TextureIntent::kAlbedo
          || desc.intent == TextureIntent::kEmissive
        ? tuning.color_output_format
        : tuning.data_output_format;
      desc.bc7_quality = desc.output_format == Format::kBC7UNorm
          || desc.output_format == Format::kBC7UNormSRGB
        ? tuning.bc7_quality
        : Bc7Quality::kNone;
    }
    source.packing_policy_id
      = tuning.enabled ? tuning.packing_policy_id : "d3d12";
    source.output_format_policy = tuning.enabled
      ? TexturePipeline::OutputFormatPolicy::kExplicit
      : TexturePipeline::OutputFormatPolicy::kMaterialPreset;
    source.failure_policy = tuning.placeholder_on_failure
      ? TexturePipeline::FailurePolicy::kPlaceholder
      : TexturePipeline::FailurePolicy::kStrict;
  }
}

} // namespace oxygen::content::import::adapters
