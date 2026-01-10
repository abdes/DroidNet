//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file TextureEmissionUtils.h
//! @brief Internal utilities for texture emission.
//!
//! This file provides emission-layer helpers for cooking and emitting textures
//! during asset import. These are internal utilities used by the FBX importer
//! and similar tools, NOT the public texture import API.
//!
//! For the public texture import API, see:
//! - TextureCooker.h: CookTexture() overloads
//! - TextureImportPresets.h: ApplyPreset() for easy configuration
//! - TextureSourceAssembly.h: TextureSourceSet for multi-source textures

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include <Oxygen/Content/Import/TextureCooker.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TexturePackingPolicy.h>
#include <Oxygen/Content/Import/emit/ResourceAppender.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::emit {

//===----------------------------------------------------------------------===//
// Cooker Integration Types
//===----------------------------------------------------------------------===//

//! Configuration for how the cooker should be used during emission.
/*!
  Controls whether the cooker is used and which features are enabled.
*/
struct CookerConfig {
  //! Whether to use the texture cooker for processing.
  bool enabled = true;

  //! Whether to generate mip maps.
  bool generate_mips = true;

  //! Whether to use BC7 compression.
  bool use_bc7_compression = false;

  //! BC7 quality preset if compression is enabled.
  Bc7Quality bc7_quality = Bc7Quality::kDefault;

  //! Packing policy ID ("d3d12" or "tight").
  std::string packing_policy_id = "d3d12";
};

//! Result of cooking a texture for emission.
struct CookedEmissionResult {
  //! PAK-format descriptor ready for serialization.
  oxygen::data::pak::TextureResourceDesc desc;

  //! Payload bytes to write to the data file.
  std::vector<std::byte> payload;

  //! Whether this was a placeholder texture due to decode failure.
  bool is_placeholder = false;
};

//===----------------------------------------------------------------------===//
// Cooker Integration API
//===----------------------------------------------------------------------===//

//! Get the packing policy for a given ID.
/*!
  @param policy_id The policy ID ("d3d12" or "tight")
  @return Pointer to the policy (static lifetime)
*/
[[nodiscard]] OXGN_CNTT_API auto GetPackingPolicy(const std::string& policy_id)
  -> const ITexturePackingPolicy&;

//! Get the default packing policy for the current platform.
[[nodiscard]] OXGN_CNTT_API auto GetDefaultPackingPolicy()
  -> const ITexturePackingPolicy&;

//! Create a TextureImportDesc from CookerConfig.
/*!
  Translates emission-time configuration into a TextureImportDesc
  suitable for the cooker.

  @param config The emission configuration
  @param texture_id Optional texture identifier for diagnostics
  @return TextureImportDesc for the cooker
*/
[[nodiscard]] OXGN_CNTT_API auto MakeImportDescFromConfig(
  const CookerConfig& config, std::string_view texture_id = {})
  -> TextureImportDesc;

//! Cook texture bytes using the texture cooker.
/*!
  Takes raw source bytes (PNG, JPG, BMP, etc.) and produces a cooked
  result ready for emission.

  @param source_bytes Raw image bytes
  @param config       Cooker configuration
  @param texture_id   Identifier for logging/diagnostics
  @return Cooked result or error
*/
[[nodiscard]] OXGN_CNTT_API auto CookTextureForEmission(
  std::span<const std::byte> source_bytes, const CookerConfig& config,
  std::string_view texture_id = {})
  -> std::expected<CookedEmissionResult, TextureImportError>;

//! Cook texture bytes with fallback to placeholder.
/*!
  Attempts to cook the texture, but on failure creates a 1x1 placeholder
  texture using a deterministic color based on the texture ID.

  @param source_bytes Raw image bytes (may be empty/invalid)
  @param config       Cooker configuration
  @param texture_id   Identifier for placeholder color and diagnostics
  @return Cooked result (always succeeds, may be placeholder)
*/
[[nodiscard]] OXGN_CNTT_API auto CookTextureWithFallback(
  std::span<const std::byte> source_bytes, const CookerConfig& config,
  std::string_view texture_id) -> CookedEmissionResult;

//! Create a fallback placeholder texture.
/*!
  Creates a 1x1 RGBA8 placeholder texture with a deterministic color
  based on the texture ID.

  @param texture_id Identifier for color generation
  @param config     Cooker configuration for packing
  @return Cooked placeholder result
*/
[[nodiscard]] OXGN_CNTT_API auto CreatePlaceholderTexture(
  std::string_view texture_id, const CookerConfig& config)
  -> CookedEmissionResult;

//! Convert CookedTexturePayload to PAK format descriptor.
/*!
  Translates the cooker's internal descriptor format to the serializable
  PAK format.

  @param payload     Cooked payload from TextureCooker
  @param data_offset Offset where payload will be written in data file
  @return PAK-format descriptor
*/
[[nodiscard]] OXGN_CNTT_API auto ToPakDescriptor(
  const CookedTexturePayload& payload, uint64_t data_offset)
  -> oxygen::data::pak::TextureResourceDesc;

} // namespace oxygen::content::import::emit
