//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/TextureImportDesc.h>

namespace oxygen::content::import::adapters {

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

} // namespace oxygen::content::import::adapters
