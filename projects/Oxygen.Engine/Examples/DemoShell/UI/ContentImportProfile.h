//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>

namespace oxygen::examples::ui {

//! The sub-sections of the Import tab's settings.
enum class ImportSettingsGroup : uint8_t {
  kContent,
  kGeometry,
  kLightsAndCameras,
  kTextures,
  kImporter,
};

//! The group's header text, such as `Lights and cameras`.
[[nodiscard]] auto to_string(ImportSettingsGroup group) noexcept
  -> std::string_view;

//! The model import settings of `profile`, as a record holds them.
[[nodiscard]] auto MakeModelImportSettings(const ImportProfile& profile)
  -> content::import::SceneImportSettings;

//! True when both importer configurations have the same thread pool and
//! pipelines.
[[nodiscard]] auto SameImporterConfig(
  const content::import::AsyncImportService::Config& lhs,
  const content::import::AsyncImportService::Config& rhs) -> bool;

//! The groups with a value off its default, in panel order.
[[nodiscard]] auto ChangedGroups(const ImportProfile& profile)
  -> std::vector<ImportSettingsGroup>;

//! `profile` with the values of `group` back to their defaults.
[[nodiscard]] auto ResetGroup(ImportProfile profile, ImportSettingsGroup group)
  -> ImportProfile;

//! The groups whose values differ between two record settings objects
//! (`ModelImportRecord::MakeSettings`), such as a record's and the panel's,
//! in panel order; the order of the two does not matter. Unreadable JSON
//! differs in every group the other holds.
[[nodiscard]] auto DifferingGroups(std::string_view lhs, std::string_view rhs)
  -> std::vector<ImportSettingsGroup>;

//! A choice of a combo: its value and the text the panel shows for it.
template <typename EnumT> struct Choice final {
  EnumT value;
  std::string_view label;
};

//! The choice's display label.
template <typename EnumT>
[[nodiscard]] constexpr auto to_string(const Choice<EnumT>& choice) noexcept
  -> std::string_view
{
  return choice.label;
}

//! The label of `value` among `choices`; `fallback` when it is not one.
template <typename EnumT, std::size_t N>
[[nodiscard]] constexpr auto LabelOf(
  const std::array<Choice<EnumT>, N>& choices, const EnumT value,
  const std::string_view fallback = "Other") -> std::string_view
{
  for (const auto& choice : choices) {
    if (choice.value == value) {
      return choice.label;
    }
  }
  return fallback;
}

namespace choices {
  using namespace content::import; // NOLINT(*-using-namespace)

  inline constexpr auto kNodePruning = std::array {
    Choice { .value = NodePruningPolicy::kKeepAll, .label = "Keep every node" },
    Choice {
      .value = NodePruningPolicy::kDropEmptyNodes,
      .label = "Drop empty nodes",
    },
  };
  inline constexpr auto kUnits = std::array {
    Choice {
      .value = UnitNormalizationPolicy::kNormalizeToMeters,
      .label = "Convert to meters",
    },
    Choice {
      .value = UnitNormalizationPolicy::kPreserveSource,
      .label = "Keep source units",
    },
    Choice {
      .value = UnitNormalizationPolicy::kApplyCustomFactor,
      .label = "Custom scale",
    },
  };
  inline constexpr auto kUpAxes = std::array {
    Choice { .value = SourceUpAxis::kFromFile, .label = "From file" },
    Choice { .value = SourceUpAxis::kY, .label = "Y up" },
    Choice { .value = SourceUpAxis::kZ, .label = "Z up" },
  };
  inline constexpr auto kGeometryPolicies = std::array {
    Choice {
      .value = GeometryAttributePolicy::kNone,
      .label = "Do not import",
    },
    Choice {
      .value = GeometryAttributePolicy::kPreserveIfPresent,
      .label = "Keep the source's",
    },
    Choice {
      .value = GeometryAttributePolicy::kGenerateMissing,
      .label = "Generate missing",
    },
    Choice {
      .value = GeometryAttributePolicy::kAlwaysRecalculate,
      .label = "Always recalculate",
    },
  };
  inline constexpr auto kIntents = std::array {
    Choice { .value = TextureIntent::kAlbedo, .label = "Albedo" },
    Choice { .value = TextureIntent::kNormalTS, .label = "Normal map" },
    Choice { .value = TextureIntent::kRoughness, .label = "Roughness" },
    Choice { .value = TextureIntent::kMetallic, .label = "Metallic" },
    Choice { .value = TextureIntent::kAO, .label = "Ambient occlusion" },
    Choice { .value = TextureIntent::kEmissive, .label = "Emissive" },
    Choice { .value = TextureIntent::kOpacity, .label = "Opacity" },
    Choice {
      .value = TextureIntent::kORMPacked,
      .label = "Occlusion, roughness, metallic",
    },
    Choice {
      .value = TextureIntent::kHdrEnvironment,
      .label = "HDR environment",
    },
    Choice {
      .value = TextureIntent::kHdrLightProbe,
      .label = "HDR light probe",
    },
    Choice { .value = TextureIntent::kData, .label = "Data" },
    Choice { .value = TextureIntent::kHeightMap, .label = "Height map" },
  };
  inline constexpr auto kColorSpaces = std::array {
    Choice { .value = ColorSpace::kLinear, .label = "Linear" },
    Choice { .value = ColorSpace::kSRGB, .label = "sRGB" },
  };
  inline constexpr auto kMipPolicies = std::array {
    Choice { .value = MipPolicy::kNone, .label = "No mips" },
    Choice { .value = MipPolicy::kFullChain, .label = "Full chain" },
    Choice { .value = MipPolicy::kMaxCount, .label = "At most N levels" },
  };
  inline constexpr auto kMipFilters = std::array {
    Choice { .value = MipFilter::kBox, .label = "Box" },
    Choice { .value = MipFilter::kKaiser, .label = "Kaiser" },
    Choice { .value = MipFilter::kLanczos, .label = "Lanczos" },
  };
  inline constexpr auto kFormats = std::array {
    Choice { .value = Format::kR8UNorm, .label = "R8" },
    Choice { .value = Format::kR8SNorm, .label = "R8 signed" },
    Choice { .value = Format::kR16Float, .label = "R16 float" },
    Choice { .value = Format::kR32Float, .label = "R32 float" },
    Choice { .value = Format::kRG8UNorm, .label = "RG8" },
    Choice { .value = Format::kRG8SNorm, .label = "RG8 signed" },
    Choice { .value = Format::kRG16Float, .label = "RG16 float" },
    Choice { .value = Format::kRG32Float, .label = "RG32 float" },
    Choice { .value = Format::kRGB32Float, .label = "RGB32 float" },
    Choice { .value = Format::kRGBA8UNorm, .label = "RGBA8" },
    Choice { .value = Format::kRGBA8UNormSRGB, .label = "RGBA8 sRGB" },
    Choice { .value = Format::kRGBA16Float, .label = "RGBA16 float" },
    Choice { .value = Format::kRGBA32Float, .label = "RGBA32 float" },
    Choice { .value = Format::kBC1UNorm, .label = "BC1" },
    Choice { .value = Format::kBC1UNormSRGB, .label = "BC1 sRGB" },
    Choice { .value = Format::kBC2UNorm, .label = "BC2" },
    Choice { .value = Format::kBC2UNormSRGB, .label = "BC2 sRGB" },
    Choice { .value = Format::kBC3UNorm, .label = "BC3" },
    Choice { .value = Format::kBC3UNormSRGB, .label = "BC3 sRGB" },
    Choice { .value = Format::kBC4UNorm, .label = "BC4" },
    Choice { .value = Format::kBC5UNorm, .label = "BC5" },
    Choice { .value = Format::kBC6HFloatU, .label = "BC6H" },
    Choice { .value = Format::kBC7UNorm, .label = "BC7" },
    Choice { .value = Format::kBC7UNormSRGB, .label = "BC7 sRGB" },
  };
  inline constexpr auto kBc7Qualities = std::array {
    Choice { .value = Bc7Quality::kNone, .label = "No BC7" },
    Choice { .value = Bc7Quality::kFast, .label = "Fast" },
    Choice { .value = Bc7Quality::kDefault, .label = "Default" },
    Choice { .value = Bc7Quality::kHigh, .label = "High" },
  };
  inline constexpr auto kHdrHandlings = std::array {
    Choice { .value = HdrHandling::kError, .label = "Refuse HDR sources" },
    Choice { .value = HdrHandling::kTonemapAuto, .label = "Tonemap to LDR" },
    Choice { .value = HdrHandling::kKeepFloat, .label = "Keep float" },
  };
  inline constexpr auto kCubeLayouts = std::array {
    Choice { .value = CubeMapImageLayout::kUnknown, .label = "Unknown" },
    Choice { .value = CubeMapImageLayout::kAuto, .label = "Detect" },
    Choice {
      .value = CubeMapImageLayout::kHorizontalStrip,
      .label = "Horizontal strip",
    },
    Choice {
      .value = CubeMapImageLayout::kVerticalStrip,
      .label = "Vertical strip",
    },
    Choice {
      .value = CubeMapImageLayout::kHorizontalCross,
      .label = "Horizontal cross",
    },
    Choice {
      .value = CubeMapImageLayout::kVerticalCross,
      .label = "Vertical cross",
    },
  };
  //! Texture size limits; 0 keeps the source size.
  inline constexpr auto kMaxSizes = std::array {
    Choice { .value = 0U, .label = "Source size" },
    Choice { .value = 4096U, .label = "4096" },
    Choice { .value = 2048U, .label = "2048" },
    Choice { .value = 1024U, .label = "1024" },
    Choice { .value = 512U, .label = "512" },
  };
} // namespace choices

} // namespace oxygen::examples::ui
