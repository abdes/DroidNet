//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/CommandRegistry.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Host/GpuWorkOverlay.h>
#include <Oxygen/Host/MainViewOptions.h>
#include <Oxygen/Host/ViewportToolbar.h>
#include <Oxygen/ImGui/Console/ConsoleUiState.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>
#include <Oxygen/ImGui/Widgets/StatsOverlay.h>
#include <Oxygen/ImGui/Widgets/Toolbar.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Diagnostics/ViewModeCatalog.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::host {

namespace {

  using imgui::MenuItem;

  constexpr std::string_view kSessionBeginCommand = "ngin.session.begin";
  constexpr float kStripAlpha = 0.65F;

  //! Renderer-wide switches listed under Advanced in the show menu.
  struct AdvancedSwitch {
    std::string_view cvar;
    std::string_view label;
  };
  constexpr auto kAdvancedSwitches = std::array {
    AdvancedSwitch {
      .cvar = "vtx.occlusion.enable",
      .label = "Occlusion",
    },
    AdvancedSwitch {
      .cvar = "vtx.shadows.contact.enable",
      .label = "Contact Shadows",
    },
  };

  struct ViewModeShortcut {
    ImGuiKey key;
    std::string_view mode;
    std::string_view text;
  };
  constexpr auto kViewModeShortcuts = std::array {
    ViewModeShortcut {
      .key = ImGuiKey_4,
      .mode = "lit",
      .text = "Alt+4",
    },
    ViewModeShortcut {
      .key = ImGuiKey_3,
      .mode = "unlit",
      .text = "Alt+3",
    },
    ViewModeShortcut {
      .key = ImGuiKey_2,
      .mode = "wireframe",
      .text = "Alt+2",
    },
  };

  auto FamilyLabel(const vortex::ShaderDebugModeFamily family)
    -> std::string_view
  {
    using vortex::ShaderDebugModeFamily;
    switch (family) {
    case ShaderDebugModeFamily::kNone:
      return "Common";
    case ShaderDebugModeFamily::kLightCulling:
      return "Light Culling";
    case ShaderDebugModeFamily::kIbl:
      return "Image-Based Lighting";
    case ShaderDebugModeFamily::kMaterial:
      return "Material";
    case ShaderDebugModeFamily::kDirectLighting:
      return "Direct Lighting";
    case ShaderDebugModeFamily::kShadow:
      return "Shadows";
    case ShaderDebugModeFamily::kSceneDepth:
      return "Scene Depth";
    case ShaderDebugModeFamily::kMasked:
      return "Masked Materials";
    }
    return "Other";
  }

  auto ReadBool(const console::Console& console, const std::string_view cvar,
    const bool fallback) -> bool
  {
    bool value = fallback;
    (void)console.TryGetCVarValue<bool>(cvar, value);
    return value;
  }

  using imgui::consoleui::LineRunner;

  //! Every toolbar interaction is a command line posted to the dispatcher;
  //! failures appear in the console.
  auto Run(const LineRunner& run, std::string line,
    const commands::CommandSource source = commands::CommandSource::kUi) -> void
  {
    run(std::move(line), source);
  }

  auto SetBool(const LineRunner& run, const std::string_view cvar,
    const bool value,
    const commands::CommandSource source = commands::CommandSource::kUi) -> void
  {
    Run(run, fmt::format("{} {}", cvar, value ? "true" : "false"), source);
  }

  auto ShortcutFor(const std::string_view mode) -> std::string_view
  {
    for (const auto& shortcut : kViewModeShortcuts) {
      if (shortcut.mode == mode) {
        return shortcut.text;
      }
    }
    return {};
  }

  //! The common modes as radio items, then one submenu per debug family.
  auto BuildViewModeItems(const LineRunner& run, const std::string_view current)
    -> std::vector<MenuItem>
  {
    std::vector<MenuItem> items;
    items.push_back(MenuItem {
      .kind = MenuItem::Kind::kSection,
      .label = std::string(FamilyLabel(vortex::ShaderDebugModeFamily::kNone)),
    });
    MenuItem* family_menu = nullptr;
    for (const auto& mode : vortex::EnumerateViewModes()) {
      auto item = MenuItem {
        .kind = MenuItem::Kind::kRadio,
        .label = std::string(mode.display_name),
        .shortcut = std::string(ShortcutFor(mode.canonical_name)),
        .tooltip = std::string(
          mode.supported ? mode.description : mode.unsupported_reason),
        .checked = mode.canonical_name == current,
        .enabled = mode.supported,
        .action = [&run, name = std::string(mode.canonical_name)] -> void {
          Run(run, fmt::format("{} {}", kViewModeCVar, name));
        },
      };
      if (mode.IsCommon()) {
        items.push_back(std::move(item));
        continue;
      }
      const auto label = std::string(FamilyLabel(mode.family));
      if (family_menu == nullptr || family_menu->label != label) {
        if (family_menu == nullptr) {
          items.push_back(
            MenuItem { .kind = MenuItem::Kind::kSection, .label = "Debug" });
        }
        items.push_back(
          MenuItem { .kind = MenuItem::Kind::kSubmenu, .label = label });
        family_menu = &items.back();
      }
      family_menu->children.push_back(std::move(item));
    }
    return items;
  }

  auto BuildShowItems(console::Console& console, const LineRunner& run)
    -> std::vector<MenuItem>
  {
    std::vector<MenuItem> items;
    for (const auto& flag : kShowFlags) {
      const bool shown = ReadBool(console, flag.cvar, true);
      items.push_back(MenuItem {
        .kind = MenuItem::Kind::kCheck,
        .label = std::string(flag.label),
        .checked = shown,
        .action = [&run, cvar = flag.cvar, shown] -> void {
          SetBool(run, cvar, !shown);
        },
      });
    }

    bool advanced_section = false;
    for (const auto& advanced : kAdvancedSwitches) {
      bool enabled = true;
      if (!console.TryGetCVarValue<bool>(advanced.cvar, enabled)) {
        continue;
      }
      if (!advanced_section) {
        items.push_back(
          MenuItem { .kind = MenuItem::Kind::kSection, .label = "Advanced" });
        advanced_section = true;
      }
      items.push_back(MenuItem {
        .kind = MenuItem::Kind::kCheck,
        .label = std::string(advanced.label),
        .tooltip = std::string(advanced.cvar),
        .checked = enabled,
        .action = [&run, cvar = advanced.cvar, enabled] -> void {
          SetBool(run, cvar, !enabled);
        },
      });
    }

    items.push_back(MenuItem { .kind = MenuItem::Kind::kSeparator });
    items.push_back(MenuItem {
      .kind = MenuItem::Kind::kAction,
      .label = "Use Defaults",
      .tooltip = "Show every feature in the main view",
      .action = [&run] -> void {
        for (const auto& flag : kShowFlags) {
          SetBool(run, flag.cvar, true);
        }
      },
    });
    return items;
  }

  auto BuildStatsItems(console::Console& console, const LineRunner& run)
    -> std::vector<MenuItem>
  {
    std::vector<MenuItem> items;
    for (const auto& stats : kStatsCVars) {
      const bool shown = ReadBool(console, stats.cvar, false);
      items.push_back(MenuItem {
        .kind = MenuItem::Kind::kCheck,
        .label = std::string(stats.label),
        .checked = shown,
        .action = [&run, cvar = stats.cvar, shown] -> void {
          SetBool(run, cvar, !shown);
        },
      });
    }
    const bool gpu_work = ReadBool(console, kGpuWorkCVar, false);
    items.push_back(MenuItem {
      .kind = MenuItem::Kind::kCheck,
      .label = "GPU Work",
      .tooltip = std::string(kGpuWorkCVar),
      .checked = gpu_work,
      .action
      = [&run, gpu_work] -> void { SetBool(run, kGpuWorkCVar, !gpu_work); },
    });
    return items;
  }

  //! The view mode's name, or the renderer-global mode marked "(global)"
  //! when the main view carries no override.
  auto ViewModeLabel(const std::string_view current,
    const observer_ptr<const vortex::Renderer> renderer) -> std::string
  {
    const auto* mode = vortex::FindViewModeInfo(current);
    if (mode == nullptr) {
      return std::string(current);
    }
    if (!mode->render_mode && !mode->shader_debug_mode && renderer) {
      const auto* global = vortex::FindViewModeInfo(
        renderer->GetRenderMode(), renderer->GetShaderDebugMode());
      if (global == nullptr) {
        return "Custom (global)";
      }
      if (global != mode) {
        return fmt::format("{} (global)", global->display_name);
      }
    }
    return std::string(mode->display_name);
  }

  auto HandleShortcuts(console::Console& console, const LineRunner& run) -> void
  {
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput) {
      return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
      SetBool(run, kUiVisibleCVar, !ReadBool(console, kUiVisibleCVar, true),
        commands::CommandSource::kKey);
    }
    if (!io.KeyAlt) {
      return;
    }
    for (const auto& shortcut : kViewModeShortcuts) {
      if (ImGui::IsKeyPressed(shortcut.key, false)) {
        Run(run, fmt::format("{} {}", kViewModeCVar, shortcut.mode),
          commands::CommandSource::kKey);
      }
    }
  }

  auto PlayStateOf(const IAsyncEngine& engine, const console::Console& console)
    -> imgui::PlayControlsState
  {
    return imgui::PlayControlsState {
      .sessions_available
      = console.Commands()->Find(kSessionBeginCommand) != nullptr,
      .session_running
      = engine.GetSessionState() != IAsyncEngine::SessionState::kNone,
      .paused = engine.IsPaused(),
    };
  }

  auto RunPlayAction(const imgui::PlayAction action,
    const imgui::PlayControlsState& state, const LineRunner& run) -> void
  {
    switch (action) {
    case imgui::PlayAction::kNone:
      return;
    case imgui::PlayAction::kPlay:
      Run(
        run, std::string(state.paused ? "ngin.resume" : kSessionBeginCommand));
      return;
    case imgui::PlayAction::kPause:
      Run(run, "ngin.pause");
      return;
    case imgui::PlayAction::kStep:
      Run(run, "ngin.step");
      return;
    case imgui::PlayAction::kStop:
      Run(run, "ngin.session.end");
      return;
    }
  }

} // namespace

auto ViewportToolbar::RegisterConsoleBindings(console::Console& console) -> void
{
  (void)console.RegisterCVar(console::CVarDefinition {
    .name = std::string(kUiVisibleCVar),
    .help = "Show the viewport toolbar and the stats overlay (F1)",
    .default_value = true,
    .flags = console::CVarFlags::kNone,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
    .allowed_values = {},
  });
  for (const auto& stats : kStatsCVars) {
    (void)console.RegisterCVar(console::CVarDefinition {
      .name = std::string(stats.cvar),
      .help = "Show " + std::string(stats.label) + " in the stats overlay",
      .default_value = false,
      .flags = console::CVarFlags::kNone,
      .min_value = std::nullopt,
      .max_value = std::nullopt,
      .allowed_values = {},
    });
  }
  GpuWorkOverlay::RegisterConsoleBindings(console);
}

auto ViewportToolbar::Draw(IAsyncEngine& engine,
  const engine::FrameContext& context,
  const observer_ptr<vortex::Renderer> renderer,
  const std::optional<ViewId> main_view,
  imgui::consoleui::ConsoleUiState& console_ui, const LineRunner& run) -> void
{
  auto& console = engine.GetConsole();
  HandleShortcuts(console, run);
  const auto stats_config = imgui::StatsOverlayConfig {
    .show_fps = ReadBool(console, kStatsCVars[0].cvar, false),
    .show_frame_timing_detail = ReadBool(console, kStatsCVars[2].cvar, false),
    .show_engine_timing = ReadBool(console, kStatsCVars[1].cvar, false),
    .show_budget_stats = ReadBool(console, kStatsCVars[3].cvar, false),
  };
  if (!ReadBool(console, kUiVisibleCVar, true)) {
    // Keep the overlay's averages warm while hidden.
    stats_overlay_.Draw(context, {});
    gpu_work_overlay_.Draw(renderer, false, main_view, {}, 0.0F);
    return;
  }
  DrawStrip(engine, renderer, console_ui, run);
  stats_overlay_.Draw(context, stats_config, strip_height_);
  gpu_work_overlay_.Draw(renderer, ReadBool(console, kGpuWorkCVar, false),
    main_view, context.GetBudgetStats().gpu_budget, strip_height_);
}

auto ViewportToolbar::ReleaseGpuTimeline() noexcept -> void
{
  gpu_work_overlay_.Release();
}

auto ViewportToolbar::DrawStrip(IAsyncEngine& engine,
  const observer_ptr<const vortex::Renderer> renderer,
  imgui::consoleui::ConsoleUiState& console_ui, const LineRunner& run) -> void
{
  auto& console = engine.GetConsole();
  const auto* viewport = ImGui::GetMainViewport();
  const auto& style = ImGui::GetStyle();
  ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(
    ImVec2(viewport->WorkSize.x,
      ImGui::GetFrameHeight() + (2.0F * style.WindowPadding.y)),
    ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(kStripAlpha);
  constexpr auto kFlags = static_cast<ImGuiWindowFlags>(
    static_cast<unsigned>(ImGuiWindowFlags_NoDecoration)
    | static_cast<unsigned>(ImGuiWindowFlags_NoMove)
    | static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings)
    | static_cast<unsigned>(ImGuiWindowFlags_NoFocusOnAppearing)
    | static_cast<unsigned>(ImGuiWindowFlags_NoBringToFrontOnFocus)
    | static_cast<unsigned>(ImGuiWindowFlags_NoNav));
  const bool visible = ImGui::Begin("Viewport Toolbar", nullptr, kFlags);
  strip_height_ = ImGui::GetWindowHeight();
  if (!visible) {
    ImGui::End();
    return;
  }

  std::string view_mode { kDefaultViewMode };
  (void)console.TryGetCVarValue<std::string>(kViewModeCVar, view_mode);
  const auto view_items = BuildViewModeItems(run, view_mode);
  const auto view_label = ViewModeLabel(view_mode, renderer);
  (void)imgui::DropdownMenu("view-mode",
    {
      .icon = ICON_FA_CIRCLE_HALF_STROKE,
      .label = view_label,
      .tooltip = "View mode",
    },
    view_items, view_mode_menu_, true);

  ImGui::SameLine();
  const auto show_items = BuildShowItems(console, run);
  (void)imgui::DropdownMenu("show",
    { .icon = ICON_FA_EYE, .label = {}, .tooltip = "Show" }, show_items,
    show_menu_);

  ImGui::SameLine();
  const auto stats_items = BuildStatsItems(console, run);
  (void)imgui::DropdownMenu("stats",
    { .icon = ICON_FA_STOPWATCH, .label = {}, .tooltip = "Stats" }, stats_items,
    stats_menu_);

  ImGui::SameLine();
  const auto play_state = PlayStateOf(engine, console);
  RunPlayAction(imgui::PlayControls(play_state), play_state, run);

  ImGui::SameLine();
  const float console_width
    = ImGui::CalcTextSize(ICON_FA_TERMINAL).x + (2.0F * style.FramePadding.x);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX()
    + std::max(0.0F, ImGui::GetContentRegionAvail().x - console_width));
  if (imgui::ToolbarButton("console",
        {
          .icon = ICON_FA_TERMINAL,
          .tooltip = "Console (`)",
          .enabled = true,
          .active = console_ui.IsConsoleVisible(),
          .dropdown_arrow = false,
        })) {
    Run(run, "ui.console");
  }
  ImGui::End();
}

} // namespace oxygen::host
