//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>

namespace oxygen::content::import {

auto to_string(const TextureIntent value) -> const char*
{
  switch (value) {
    // clang-format off
    case TextureIntent::kAlbedo:         return "Albedo";
    case TextureIntent::kNormalTS:       return "NormalTS";
    case TextureIntent::kRoughness:      return "Roughness";
    case TextureIntent::kMetallic:       return "Metallic";
    case TextureIntent::kAO:             return "AO";
    case TextureIntent::kEmissive:       return "Emissive";
    case TextureIntent::kOpacity:        return "Opacity";
    case TextureIntent::kORMPacked:      return "ORMPacked";
    case TextureIntent::kHdrEnvironment: return "HdrEnvironment";
    case TextureIntent::kHdrLightProbe:  return "HdrLightProbe";
    case TextureIntent::kData:           return "Data";
    // clang-format on
  }

  return "__NotSupported__";
}

auto to_string(const MipPolicy value) -> const char*
{
  switch (value) {
    // clang-format off
    case MipPolicy::kNone:      return "None";
    case MipPolicy::kFullChain: return "FullChain";
    case MipPolicy::kMaxCount:  return "MaxCount";
    // clang-format on
  }

  return "__NotSupported__";
}

auto to_string(const MipFilter value) -> const char*
{
  switch (value) {
    // clang-format off
    case MipFilter::kBox:     return "Box";
    case MipFilter::kKaiser:  return "Kaiser";
    case MipFilter::kLanczos: return "Lanczos";
    // clang-format on
  }

  return "__NotSupported__";
}

auto to_string(const Bc7Quality value) -> const char*
{
  switch (value) {
    // clang-format off
    case Bc7Quality::kNone:    return "None";
    case Bc7Quality::kFast:    return "Fast";
    case Bc7Quality::kDefault: return "Default";
    case Bc7Quality::kHigh:    return "High";
    // clang-format on
  }

  return "__NotSupported__";
}

auto to_string(const HdrHandling value) -> const char*
{
  switch (value) {
    // clang-format off
    case HdrHandling::kError:       return "Error";
    case HdrHandling::kTonemapAuto: return "TonemapAuto";
    case HdrHandling::kKeepFloat:   return "KeepFloat";
    // clang-format on
  }

  return "__NotSupported__";
}

auto to_string(const TextureImportError value) -> const char*
{
  switch (value) {
    // clang-format off
    // Success
    case TextureImportError::kSuccess:               return "Success";

    // Decode errors
    case TextureImportError::kUnsupportedFormat:     return "UnsupportedFormat";
    case TextureImportError::kCorruptedData:         return "CorruptedData";
    case TextureImportError::kDecodeFailed:          return "DecodeFailed";
    case TextureImportError::kOutOfMemory:           return "OutOfMemory";

    // Validation errors
    case TextureImportError::kInvalidDimensions:     return "InvalidDimensions";
    case TextureImportError::kDimensionMismatch:     return "DimensionMismatch";
    case TextureImportError::kArrayLayerCountInvalid: return "ArrayLayerCountInvalid";
    case TextureImportError::kDepthInvalidFor2D:     return "DepthInvalidFor2D";
    case TextureImportError::kInvalidMipPolicy:      return "InvalidMipPolicy";
    case TextureImportError::kInvalidOutputFormat:   return "InvalidOutputFormat";
    case TextureImportError::kIntentFormatMismatch:  return "IntentFormatMismatch";

    // Cook errors
    case TextureImportError::kMipGenerationFailed:   return "MipGenerationFailed";
    case TextureImportError::kCompressionFailed:     return "CompressionFailed";
    case TextureImportError::kOutputFormatInvalid:   return "OutputFormatInvalid";
    case TextureImportError::kHdrRequiresFloatFormat: return "HdrRequiresFloatFormat";

    // I/O errors
    case TextureImportError::kFileNotFound:          return "FileNotFound";
    case TextureImportError::kFileReadFailed:        return "FileReadFailed";
    case TextureImportError::kWriteFailed:           return "WriteFailed";
    // clang-format on
  }

  return "__NotSupported__";
}

} // namespace oxygen::content::import
