//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Diagnostics/ViewModeCatalog.h>

namespace oxygen::host {

namespace {

  using Mask = vortex::CompositionView::ViewFeatureMask;

  consteval auto ShowFlagsFollowTheirEnum() -> bool
  {
    for (std::size_t i = 0; i < kShowFlags.size(); ++i) {
      if (static_cast<std::size_t>(kShowFlags.at(i).flag) != i) {
        return false;
      }
    }
    return true;
  }
  static_assert(ShowFlagsFollowTheirEnum());

  auto SelectableViewModes() -> std::vector<std::string>
  {
    std::vector<std::string> names;
    for (const auto& mode : vortex::EnumerateViewModes()) {
      if (mode.supported) {
        names.emplace_back(mode.canonical_name);
      }
    }
    return names;
  }

  auto ClearFeature(vortex::CompositionView& view, const Mask::Bit bit) -> void
  {
    view.feature_mask.bits = vortex::CompositionView::ViewFeatureBits {
      view.feature_mask.bits.get() & ~static_cast<std::uint64_t>(bit),
    };
  }

} // namespace

auto MainViewOptions::RegisterConsoleBindings(console::Console& console) -> void
{
  (void)console.RegisterCVar(console::CVarDefinition {
    .name = std::string(kViewModeCVar),
    .help = "View mode of the main view: lit, unlit, wireframe or a debug mode",
    .default_value = std::string(kDefaultViewMode),
    .flags = console::CVarFlags::kArchive,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
    .allowed_values = SelectableViewModes(),
  });
  for (const auto& info : kShowFlags) {
    (void)console.RegisterCVar(console::CVarDefinition {
      .name = std::string(info.cvar),
      .help = "Show " + std::string(info.label) + " in the main view",
      .default_value = true,
      .flags = console::CVarFlags::kArchive,
      .min_value = std::nullopt,
      .max_value = std::nullopt,
      .allowed_values = {},
    });
  }
}

auto MainViewOptions::FromConsole(const console::Console& console)
  -> MainViewOptions
{
  MainViewOptions options;
  (void)console.TryGetCVarValue<std::string>(kViewModeCVar, options.view_mode);
  for (const auto& info : kShowFlags) {
    (void)console.TryGetCVarValue<bool>(
      info.cvar, options.show.at(static_cast<std::size_t>(info.flag)));
  }
  return options;
}

auto MainViewOptions::ApplyTo(vortex::CompositionView& view) const -> void
{
  // An unknown name cannot pass the console; treat it as lit regardless.
  if (const auto* mode = vortex::FindViewModeInfo(view_mode);
    mode != nullptr && mode->supported) {
    view.render_settings.render_mode = mode->render_mode;
    view.render_settings.shader_debug_mode = mode->shader_debug_mode;
  } else {
    view.render_settings.render_mode.reset();
    view.render_settings.shader_debug_mode.reset();
  }

  const auto clear_unless
    = [&](const ShowFlag flag, const Mask::Bit bit) -> void {
    if (!Shows(flag)) {
      ClearFeature(view, bit);
    }
  };
  clear_unless(ShowFlag::kShadows, Mask::kShadows);
  clear_unless(ShowFlag::kEnvironment, Mask::kEnvironment);
  clear_unless(ShowFlag::kTranslucency, Mask::kTranslucency);
  clear_unless(ShowFlag::kVolumetrics, Mask::kVolumetrics);
  clear_unless(ShowFlag::kGrid, Mask::kGroundGrid);
  view.with_atmosphere = view.with_atmosphere && Shows(ShowFlag::kAtmosphere);
  view.with_height_fog = view.with_height_fog && Shows(ShowFlag::kHeightFog);
  view.with_local_fog = view.with_local_fog && Shows(ShowFlag::kLocalFog);
}

auto MainViewOptions::ApplyToMain(std::span<vortex::CompositionView> views,
  const std::optional<ViewId> main) const -> void
{
  if (!main) {
    return;
  }
  for (auto& view : views) {
    if (view.id == *main) {
      ApplyTo(view);
      return;
    }
  }
}

} // namespace oxygen::host
