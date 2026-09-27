//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "Async/AsyncDemoPanel.h"
#include "Async/AsyncDemoVm.h"
#include <fmt/format.h>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

// ImGui
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace oxygen::examples::async {

AsyncDemoPanel::AsyncDemoPanel(observer_ptr<AsyncDemoVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "AsyncDemoPanel requires AsyncDemoVm");
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
  // Use a generic settings icon or list icon
  return oxygen::imgui::icons::kIconDemoPanel;
}

auto AsyncDemoPanel::DrawContents() -> void
{
  DrawSceneInfo();
  DrawSpotlightControls();
  DrawProfilingInfo();
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
      vm_->SetAnimationEnabled(animate);
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
        vm_->EnsureSpotlight();
      }
    } else {
      ImGui::TextWrapped("Follows the camera from an offset position so cast "
                         "shadows remain visible.");
      bool enabled = vm_->GetSpotlightEnabled();
      if (ImGui::Checkbox("Enabled", &enabled)) {
        vm_->SetSpotlightEnabled(enabled);
      }

      bool shadows = vm_->GetSpotlightCastsShadows();
      if (ImGui::Checkbox("Cast Shadows", &shadows)) {
        vm_->SetSpotlightCastsShadows(shadows);
      }

      constexpr float kMaximumFluxLm = 50000.0F;
      constexpr float kMaximumConeDegrees = 89.0F;
      float intensity = vm_->GetSpotlightIntensity();
      if (ImGui::SliderFloat("Flux (lm)", &intensity, 0.0F, kMaximumFluxLm,
            "%.0f lm", ImGuiSliderFlags_Logarithmic)) {
        vm_->SetSpotlightIntensity(intensity);
      }

      float range = vm_->GetSpotlightRange();
      if (ImGui::SliderFloat("Range", &range, 1.0F, 100.0F, "%.1f m")) {
        vm_->SetSpotlightRange(range);
      }

      // Cones
      float inner = glm::degrees(vm_->GetSpotlightInnerCone());
      float outer = glm::degrees(vm_->GetSpotlightOuterCone());

      if (ImGui::SliderFloat(
            "Inner Cone", &inner, 1.0F, kMaximumConeDegrees, "%.1F deg")) {
        inner = std::min(inner, outer);
        vm_->SetSpotlightInnerCone(glm::radians(inner));
      }
      if (ImGui::SliderFloat(
            "Outer Cone", &outer, 1.0F, kMaximumConeDegrees, "%.1F deg")) {
        outer = std::max(outer, inner);
        vm_->SetSpotlightOuterCone(glm::radians(outer));
      }
    }
  }
}

void AsyncDemoPanel::DrawProfilingInfo()
{
  const bool was_open = vm_->GetProfilerSectionOpen();
  ImGui::SetNextItemOpen(was_open);
  const bool open = ImGui::CollapsingHeader("Frame Profiling");
  if (open != was_open) {
    vm_->SetProfilerSectionOpen(open);
  }
  if (open) {
    ImGui::TextWrapped("Previous completed frame: demo CPU callbacks. Use "
                       "Diagnostics for renderer CPU/GPU timings.");

    const auto& actions = vm_->GetFrameActions();
    if (!actions.empty()) {
      ImGui::Text("Frame Actions:");
      constexpr float kActionLogHeight = 100.0F;
      ImGui::BeginChild(
        "ActionLog", ImVec2(0, kActionLogHeight), ImGuiChildFlags_Borders);
      for (const auto& action : actions) {
        ImGui::TextUnformatted(action.c_str());
      }
      ImGui::EndChild();
    }

    const auto& timings = vm_->GetPhaseTimings();
    if (!timings.empty()) {
      ImGui::NewLine();
      ImGui::Text("Phase Timings:");
      if (ImGui::BeginTable("Timings", 2,
            static_cast<ImGuiTableFlags>(
              static_cast<unsigned>(ImGuiTableFlags_Borders)
              | static_cast<unsigned>(ImGuiTableFlags_RowBg)))) {
        ImGui::TableSetupColumn("Phase");
        ImGui::TableSetupColumn("Duration (us)");
        ImGui::TableHeadersRow();

        for (const auto& [phase, duration] : timings) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(phase.c_str());
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(fmt::format("{}", duration.count()).c_str());
        }
        ImGui::EndTable();
      }
    }
  }
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg)

} // namespace oxygen::examples::async
