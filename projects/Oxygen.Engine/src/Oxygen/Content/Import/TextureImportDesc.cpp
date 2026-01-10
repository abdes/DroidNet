//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureImportDesc.h>

namespace oxygen::content::import {

namespace {

  //! Check if the format is a floating-point format.
  [[nodiscard]] constexpr auto IsFloatFormat(Format format) noexcept -> bool
  {
    switch (format) {
    case Format::kR16Float:
    case Format::kRG16Float:
    case Format::kRGBA16Float:
    case Format::kR32Float:
    case Format::kRG32Float:
    case Format::kRGB32Float:
    case Format::kRGBA32Float:
    case Format::kR11G11B10Float:
    case Format::kBC6HFloatU:
    case Format::kBC6HFloatS:
      return true;
    default:
      return false;
    }
  }

  //! Check if the format is a BC7 format.
  [[nodiscard]] constexpr auto IsBc7Format(Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

  //! Check if the intent implies HDR content.
  [[nodiscard]] constexpr auto IsHdrIntent(TextureIntent intent) noexcept
    -> bool
  {
    return intent == TextureIntent::kHdrEnvironment
      || intent == TextureIntent::kHdrLightProbe;
  }

} // namespace

auto TextureImportDesc::Validate() const noexcept
  -> std::optional<TextureImportError>
{
  // Check dimensions
  if (width == 0 || height == 0) {
    return TextureImportError::kInvalidDimensions;
  }

  // Check depth for non-3D textures
  if (texture_type != TextureType::kTexture3D && depth != 1) {
    return TextureImportError::kDepthInvalidFor2D;
  }

  // Check array layer count based on texture type
  switch (texture_type) {
  case TextureType::kTexture2D:
  case TextureType::kTexture3D:
    if (array_layers != 1) {
      return TextureImportError::kArrayLayerCountInvalid;
    }
    break;

  case TextureType::kTextureCube:
    if (array_layers != 6) {
      return TextureImportError::kArrayLayerCountInvalid;
    }
    break;

  case TextureType::kTextureCubeArray:
    if (array_layers == 0 || (array_layers % 6) != 0) {
      return TextureImportError::kArrayLayerCountInvalid;
    }
    break;

  case TextureType::kTexture2DArray:
  case TextureType::kTexture1DArray:
    if (array_layers == 0) {
      return TextureImportError::kArrayLayerCountInvalid;
    }
    break;

  default:
    break;
  }

  // Check mip policy
  if (mip_policy == MipPolicy::kMaxCount && max_mip_levels == 0) {
    return TextureImportError::kInvalidMipPolicy;
  }

  // Check HDR content vs output format
  if (IsHdrIntent(intent) && !bake_hdr_to_ldr) {
    if (!IsFloatFormat(output_format)) {
      return TextureImportError::kHdrRequiresFloatFormat;
    }
  }

  // Check BC7 quality vs output format consistency
  if (bc7_quality != Bc7Quality::kNone && !IsBc7Format(output_format)) {
    return TextureImportError::kIntentFormatMismatch;
  }

  // Check that BC7 format implies BC7 quality is set
  if (IsBc7Format(output_format) && bc7_quality == Bc7Quality::kNone) {
    return TextureImportError::kIntentFormatMismatch;
  }

  return std::nullopt;
}

} // namespace oxygen::content::import
