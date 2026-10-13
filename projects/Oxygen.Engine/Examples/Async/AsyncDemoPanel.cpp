//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "Async/AsyncDemoPanel.h"
#include "Async/AsyncDemoVm.h"
#include "Async/AsyncShowcase.h"
#include "DemoShell/PanelCommands.h"
#include <fmt/format.h>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

// ImGui
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace oxygen::examples::async {

namespace {

  constexpr std::string_view kPlay = "async.tour.play";
  constexpr std::string_view kPause = "async.tour.pause";
  constexpr std::string_view kExplore = "async.tour.explore";
  constexpr std::string_view kCompare = "async.tour.compare";
  constexpr std::string_view kLighting = "async.lighting";
  constexpr std::string_view kAnimate = "async.animate";
  constexpr std::string_view kSpotCreate = "async.spot.create";
  constexpr std::string_view kSpotEnabled = "async.spot.enabled";
  constexpr std::string_view kSpotShadows = "async.spot.shadows";
  constexpr std::string_view kSpotFlux = "async.spot.flux";
  constexpr std::string_view kSpotRange = "async.spot.range";
  constexpr std::string_view kSpotInner = "async.spot.inner";
  constexpr std::string_view kSpotOuter = "async.spot.outer";

  constexpr auto kLightings = std::array {
    std::pair { ShowcaseLighting::kDaylight, "daylight" },
    std::pair { ShowcaseLighting::kSunset, "sunset" },
    std::pair { ShowcaseLighting::kSpotlight, "spotlight" },
  };

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

} // namespace

AsyncDemoPanel::AsyncDemoPanel(
  observer_ptr<AsyncDemoVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "AsyncDemoPanel requires AsyncDemoVm");
  DCHECK_NOTNULL_F(commands_, "AsyncDemoPanel requires PanelCommands");
  RegisterCommands();
}

auto AsyncDemoPanel::RegisterCommands() -> void
{
  const auto vm = vm_;
  // The tour exists once the scene is ready; until then these do nothing.
  const auto with_showcase
    = [vm](const std::function<void(AsyncShowcase&)>& act) -> void {
    if (const auto showcase = vm->GetShowcase()) {
      act(*showcase);
    }
  };
  auto& commands = *commands_;
  commands.AddAction(Name(kPlay), "Play Tour", "Play the tour from its start",
    [with_showcase] -> void {
      with_showcase([](AsyncShowcase& tour) -> void { tour.Play(); });
    });
  commands.AddAction(Name(kPause), "Pause Tour",
    "Pause the tour, or resume it when paused", [with_showcase] -> void {
      with_showcase([](AsyncShowcase& tour) -> void { tour.Pause(); });
    });
  commands.AddAction(Name(kExplore), "Explore",
    "Leave the tour and restore your scene and camera settings",
    [with_showcase] -> void {
      with_showcase([](AsyncShowcase& tour) -> void { tour.Explore(); });
    });
  commands.AddBool(Name(kCompare), "Compare",
    "Show the comparison the tour step describes while held",
    [with_showcase](const bool held) -> void {
      with_showcase(
        [held](AsyncShowcase& tour) -> void { tour.SetComparisonHeld(held); });
    });
  commands.AddEnum(Name(kLighting), "Lighting Preview",
    "Preview a lighting setup", { "daylight", "sunset", "spotlight" },
    [with_showcase](const std::size_t i) -> void {
      with_showcase([i](AsyncShowcase& tour) -> void {
        tour.Preview(kLightings.at(i).first);
      });
    });
  commands.AddBool(Name(kAnimate), "Animate Scene", "Animate the spheres",
    [vm](const bool enabled) -> void { vm->SetAnimationEnabled(enabled); });
  commands.AddAction(Name(kSpotCreate), "Create Spotlight",
    "Attach the spotlight to the camera",
    [vm] -> void { vm->EnsureSpotlight(); });
  commands.AddBool(Name(kSpotEnabled), "Spotlight",
    "Turn the camera spotlight on or off",
    [vm](const bool v) -> void { vm->SetSpotlightEnabled(v); });
  commands.AddBool(Name(kSpotShadows), "Spotlight Shadows",
    "Whether the camera spotlight casts shadows",
    [vm](const bool v) -> void { vm->SetSpotlightCastsShadows(v); });
  commands.AddFloat(
    Name(kSpotFlux), "Spotlight Flux",
    "Luminous flux of the camera spotlight, in lumens",
    [vm](const float v) -> void { vm->SetSpotlightIntensity(v); }, 0.0F);
  commands.AddFloat(
    Name(kSpotRange), "Spotlight Range",
    "Range of the camera spotlight, in meters",
    [vm](const float v) -> void { vm->SetSpotlightRange(v); }, 1.0F, 100.0F);
  commands.AddFloat(
    Name(kSpotInner), "Spotlight Inner Cone", "Inner cone angle, in degrees",
    [vm](const float v) -> void { vm->SetSpotlightInnerCone(glm::radians(v)); },
    0.0F, 90.0F);
  commands.AddFloat(
    Name(kSpotOuter), "Spotlight Outer Cone", "Outer cone angle, in degrees",
    [vm](const float v) -> void { vm->SetSpotlightOuterCone(glm::radians(v)); },
    0.0F, 90.0F);
}

auto AsyncDemoPanel::GetName() const noexcept -> std::string_view
{
  return "Async Demo";
}

auto AsyncDemoPanel::GetPreferredWidth() const noexcept -> float
{
  constexpr float kPanelWidth = 360.0F;
  return kPanelWidth;
}

auto AsyncDemoPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconDemoPanel;
}

auto AsyncDemoPanel::DrawContents() -> void
{
  DrawShowcaseControls();
  DrawSceneInfo();
  DrawSpotlightControls();
}

auto AsyncDemoPanel::DrawShowcaseControls() -> void
{
  const auto showcase = vm_->GetShowcase();
  if (!showcase) {
    return;
  }
  const char* playback_label = "Play tour";
  if (showcase->IsPlaying()) {
    playback_label = "Pause";
  } else if (showcase->IsPaused()) {
    playback_label = "Resume";
  }
  if (ImGui::Button(playback_label)) {
    commands_->Run(
      showcase->IsPlaying() || showcase->IsPaused() ? kPause : kPlay);
  }
  if (showcase->IsActive()) {
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
      commands_->Run(kPlay);
    }
    ImGui::SameLine();
    if (ImGui::Button("Explore")) {
      commands_->Run(kExplore);
    }
    ImGui::SetItemTooltip("Restore your scene and camera settings");
  }
  if (ImGui::Button("Daylight")) {
    commands_->RunText(kLighting, "daylight");
  }
  ImGui::SameLine();
  if (ImGui::Button("Sunset")) {
    commands_->RunText(kLighting, "sunset");
  }
  ImGui::SameLine();
  if (ImGui::Button("Spotlight##LightingPreset")) {
    commands_->RunText(kLighting, "spotlight");
  }
}

auto AsyncDemoPanel::DrawPresentation(
  const frame::SequenceNumber frame_sequence, const float top_inset) -> void
{
  const auto showcase = vm_->GetShowcase();
  if (!showcase || !showcase->IsActive()) {
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    commands_->Run(kExplore);
    return;
  }
  constexpr float kPadding = 16.0F;
  constexpr float kMaximumWidth = 700.0F;
  constexpr float kHeightInTextLines = 8.0F;
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({ viewport->WorkPos.x + kPadding,
    viewport->WorkPos.y + top_inset + kPadding });
  ImGui::SetNextWindowSize(
    { std::min(kMaximumWidth, viewport->WorkSize.x - (2.0F * kPadding)),
      ImGui::GetTextLineHeightWithSpacing() * kHeightInTextLines });
  if (ImGui::Begin(
        "Async presentation", nullptr, ImGuiWindowFlags_NoDecoration)) {
    ImGui::TextUnformatted(
      fmt::format("Frame sequence: {}", frame_sequence.get()).c_str());
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(showcase->Caption());
    ImGui::PopTextWrapPos();
    DrawShowcaseControls();
    ImGui::Button(showcase->ComparisonLabel());
    // The comparison shows while the button is held: run on press and
    // release only.
    if (const bool held = ImGui::IsItemActive(); held != comparison_held_) {
      comparison_held_ = held;
      commands_->RunBool(kCompare, held);
    }
    if (showcase->IsPlaying() || showcase->IsPaused()) {
      ImGui::ProgressBar(
        showcase->Progress(), { -1.0F, 0.0F }, "60-second tour");
    }
  }
  ImGui::End();
}

void AsyncDemoPanel::DrawSceneInfo()
{
  bool open = vm_->GetSceneSectionOpen();

  // Sync collapse state with VM
  ImGui::SetNextItemOpen(open);
  if (ImGui::CollapsingHeader("Scene Info")) {
    if (!open) {
      vm_->SetSceneSectionOpen(true);
    }

    bool animate = vm_->IsAnimationEnabled();
    if (ImGui::Checkbox("Animate scene", &animate)) {
      commands_->RunBool(kAnimate, animate);
    }
    ImGui::Text("Animation Time: %.2F s", vm_->GetAnimationTime());
    ImGui::Text("Spheres: %zu", vm_->GetSphereCount());
    ImGui::TextWrapped(
      "Opaque grid: roughness increases along +X, metalness along +Y. "
      "Translucent dielectrics orbit outside the grid.");

    constexpr std::size_t kVisibleSphereDetails = 5U;
    if ((vm_->GetSphereCount() > 0) && ImGui::TreeNode("Sphere Details")) {
      for (size_t i = 0; i < vm_->GetSphereCount() && i < kVisibleSphereDetails;
        ++i) {
        ImGui::TextUnformatted(vm_->GetSphereInfo(i).c_str());
      }
      if (vm_->GetSphereCount() > kVisibleSphereDetails) {
        ImGui::TextDisabled(
          "... and %zu more", vm_->GetSphereCount() - kVisibleSphereDetails);
      }
      ImGui::TreePop();
    }

  } else {
    if (open) {
      vm_->SetSceneSectionOpen(false);
    }
  }
}

void AsyncDemoPanel::DrawSpotlightControls()
{
  const bool was_open = vm_->GetSpotlightSectionOpen();
  ImGui::SetNextItemOpen(was_open);
  const bool open = ImGui::CollapsingHeader("Spotlight");
  if (open != was_open) {
    vm_->SetSpotlightSectionOpen(open);
  }
  if (open) {

    if (!vm_->IsSpotlightAvailable()) {
      ImGui::TextColored(ImVec4(1, 1, 0, 1), "Spotlight not created yet.");
      if (ImGui::Button("Create Spotlight")) {
        commands_->Run(kSpotCreate);
      }
    } else {
      ImGui::TextWrapped("Follows the camera from an offset position so cast "
                         "shadows remain visible.");
      bool enabled = vm_->GetSpotlightEnabled();
      if (ImGui::Checkbox("Enabled", &enabled)) {
        commands_->RunBool(kSpotEnabled, enabled);
      }

      bool shadows = vm_->GetSpotlightCastsShadows();
      if (ImGui::Checkbox("Cast Shadows", &shadows)) {
        commands_->RunBool(kSpotShadows, shadows);
      }

      constexpr float kMaximumFluxLm = 50000.0F;
      constexpr float kMaximumConeDegrees = 89.0F;
      float intensity = vm_->GetSpotlightIntensity();
      if (ImGui::SliderFloat("Flux (lm)", &intensity, 0.0F, kMaximumFluxLm,
            "%.0f lm", ImGuiSliderFlags_Logarithmic)) {
        commands_->RunFloat(kSpotFlux, intensity);
      }

      float range = vm_->GetSpotlightRange();
      if (ImGui::SliderFloat("Range", &range, 1.0F, 100.0F, "%.1f m")) {
        commands_->RunFloat(kSpotRange, range);
      }

      // Cones
      float inner = glm::degrees(vm_->GetSpotlightInnerCone());
      float outer = glm::degrees(vm_->GetSpotlightOuterCone());

      if (ImGui::SliderFloat(
            "Inner Cone", &inner, 1.0F, kMaximumConeDegrees, "%.1F deg")) {
        inner = std::min(inner, outer);
        commands_->RunFloat(kSpotInner, inner);
      }
      if (ImGui::SliderFloat(
            "Outer Cone", &outer, 1.0F, kMaximumConeDegrees, "%.1F deg")) {
        outer = std::max(outer, inner);
        commands_->RunFloat(kSpotOuter, outer);
      }
    }
  }
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)

} // namespace oxygen::examples::async
