//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Diagnostics/ViewModeCatalog.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen::vortex {

namespace {

  struct CommonMode {
    std::string_view canonical_name;
    std::string_view display_name;
    std::string_view description;
    std::optional<RenderMode> render_mode;
    std::optional<ShaderDebugMode> shader_debug_mode;
  };

  // The editor's viewport offers the same set, in the same order.
  constexpr auto kCommonModes = std::array {
    CommonMode {
      .canonical_name = "lit",
      .display_name = "Lit",
      .description = "The fully lit scene",
      .render_mode = std::nullopt,
      .shader_debug_mode = std::nullopt,
    },
    CommonMode {
      .canonical_name = "unlit",
      .display_name = "Unlit",
      .description = "Base color without lighting",
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kBaseColor,
    },
    CommonMode {
      .canonical_name = "wireframe",
      .display_name = "Wireframe",
      .description = "Geometry edges only",
      .render_mode = RenderMode::kWireframe,
      .shader_debug_mode = std::nullopt,
    },
    CommonMode {
      .canonical_name = "lit-wireframe",
      .display_name = "Lit + Wireframe",
      .description = "Edges over the lit scene",
      .render_mode = RenderMode::kOverlayWireframe,
      .shader_debug_mode = std::nullopt,
    },
    CommonMode {
      .canonical_name = "direct-lighting",
      .display_name = "Direct Lighting",
      .description = "Light from scene lights only",
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kDirectLightingOnly,
    },
    CommonMode {
      .canonical_name = "indirect-lighting",
      .display_name = "Indirect Lighting",
      .description = "Light from the sky only",
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kIblOnly,
    },
    CommonMode {
      .canonical_name = "world-normals",
      .display_name = "World Normals",
      .description = {},
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kWorldNormals,
    },
    CommonMode {
      .canonical_name = "roughness",
      .display_name = "Roughness",
      .description = {},
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kRoughness,
    },
    CommonMode {
      .canonical_name = "metalness",
      .display_name = "Metalness",
      .description = {},
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kMetalness,
    },
    CommonMode {
      .canonical_name = "linear-depth",
      .display_name = "Scene Depth",
      .description = "Linear scene depth",
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kSceneDepthLinear,
    },
    CommonMode {
      .canonical_name = "shadow-mask",
      .display_name = "Shadow Mask",
      .description = "Directional light shadowing",
      .render_mode = std::nullopt,
      .shader_debug_mode = ShaderDebugMode::kDirectionalShadowMask,
    },
  };

  auto IsCommonDebugMode(const ShaderDebugMode mode) noexcept -> bool
  {
    return std::ranges::any_of(
      kCommonModes, [mode](const CommonMode& common) -> bool {
        return common.shader_debug_mode == mode;
      });
  }

  auto BuildCatalog() -> std::vector<ViewModeInfo>
  {
    std::vector<ViewModeInfo> catalog;
    catalog.reserve(kCommonModes.size() + EnumerateShaderDebugModes().size());
    for (const auto& common : kCommonModes) {
      const auto* debug = common.shader_debug_mode
        ? FindShaderDebugModeInfo(*common.shader_debug_mode)
        : nullptr;
      catalog.push_back(ViewModeInfo {
        .canonical_name = common.canonical_name,
        .display_name = common.display_name,
        .description = common.description,
        .family = ShaderDebugModeFamily::kNone,
        .render_mode = common.render_mode,
        .shader_debug_mode = common.shader_debug_mode,
        .supported = debug == nullptr || debug->supported,
        .unsupported_reason
        = debug != nullptr ? debug->unsupported_reason : std::string_view {},
      });
    }

    const auto first_family = catalog.size();
    for (const auto& debug : EnumerateShaderDebugModes()) {
      if (debug.mode == ShaderDebugMode::kDisabled
        || IsCommonDebugMode(debug.mode)) {
        continue;
      }
      catalog.push_back(ViewModeInfo {
        .canonical_name = debug.canonical_name,
        .display_name = debug.display_name,
        .description = {},
        .family = debug.family,
        .render_mode = std::nullopt,
        .shader_debug_mode = debug.mode,
        .supported = debug.supported,
        .unsupported_reason = debug.unsupported_reason,
      });
    }
    // Group by family, keeping the registry order inside each family.
    std::ranges::stable_sort(
      catalog.begin() + static_cast<std::ptrdiff_t>(first_family),
      catalog.end(), {}, &ViewModeInfo::family);
    return catalog;
  }

  auto Catalog() noexcept -> const std::vector<ViewModeInfo>&
  {
    static const auto catalog = BuildCatalog();
    return catalog;
  }

} // namespace

auto EnumerateViewModes() noexcept -> std::span<const ViewModeInfo>
{
  return Catalog();
}

auto FindViewModeInfo(const std::string_view canonical_name) noexcept
  -> const ViewModeInfo*
{
  const auto& catalog = Catalog();
  const auto found
    = std::ranges::find(catalog, canonical_name, &ViewModeInfo::canonical_name);
  return found != catalog.end() ? &*found : nullptr;
}

auto FindViewModeInfo(const RenderMode render_mode,
  const ShaderDebugMode shader_debug_mode) noexcept -> const ViewModeInfo*
{
  const auto& catalog = Catalog();
  const auto found
    = std::ranges::find_if(catalog, [&](const ViewModeInfo& info) -> bool {
        return info.render_mode.value_or(RenderMode::kSolid) == render_mode
          && info.shader_debug_mode.value_or(ShaderDebugMode::kDisabled)
          == shader_debug_mode;
      });
  return found != catalog.end() ? &*found : nullptr;
}

} // namespace oxygen::vortex
