//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include <Oxygen/Content/Import/CookedContentWriter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/emit/ResourceAppender.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>
#include <Oxygen/Content/Import/fbx/ufbx.h>

namespace oxygen::content::import::emit {

//! Ensures the fallback texture (index 0) exists in the emission state.
/*!
 The fallback is a 1x1 white RGBA8 texture used when texture loading fails.

 @param state The texture emission state.
*/
auto EnsureFallbackTexture(TextureEmissionState& state) -> void;

//! Gets or creates a texture resource index using the cooker pipeline.
/*!
 Enhanced version that uses the texture cooker for mip generation and
 optional BC7 compression. Falls back to placeholder on decode failure.

 @param request The import request with source path info.
 @param cooked_out Writer for diagnostics.
 @param state The texture emission state.
 @param texture The ufbx texture (may be null).
 @param config Cooker configuration for mips, compression, and packing.
 @return The texture resource index (0 for fallback).
*/
[[nodiscard]] auto GetOrCreateTextureResourceIndexWithCooker(
  const ImportRequest& request, CookedContentWriter& cooked_out,
  TextureEmissionState& state, const ufbx_texture* texture,
  const CookerConfig& config) -> uint32_t;

//! Resolves a ufbx texture to its file texture.
/*!
 Some textures in FBX are procedural and reference other file textures.

 @param texture The texture to resolve.
 @return The file texture, or nullptr.
*/
[[nodiscard]] auto ResolveFileTexture(const ufbx_texture* texture)
  -> const ufbx_texture*;

//! Gets the identifier string for a texture.
[[nodiscard]] auto TextureIdString(const ufbx_texture& texture)
  -> std::string_view;

//! Normalizes a texture path for use as an ID.
[[nodiscard]] auto NormalizeTexturePathId(std::filesystem::path p)
  -> std::string;

//! Selects the base color texture from a material.
[[nodiscard]] auto SelectBaseColorTexture(const ufbx_material& material)
  -> const ufbx_texture*;

//! Selects the normal map texture from a material.
[[nodiscard]] auto SelectNormalTexture(const ufbx_material& material)
  -> const ufbx_texture*;

//! Selects the metallic texture from a material.
[[nodiscard]] auto SelectMetallicTexture(const ufbx_material& material)
  -> const ufbx_texture*;

//! Selects the roughness texture from a material.
[[nodiscard]] auto SelectRoughnessTexture(const ufbx_material& material)
  -> const ufbx_texture*;

//! Selects the ambient occlusion texture from a material.
[[nodiscard]] auto SelectAmbientOcclusionTexture(const ufbx_material& material)
  -> const ufbx_texture*;

//! Selects the emissive texture from a material.
[[nodiscard]] auto SelectEmissiveTexture(const ufbx_material& material)
  -> const ufbx_texture*;

} // namespace oxygen::content::import::emit
