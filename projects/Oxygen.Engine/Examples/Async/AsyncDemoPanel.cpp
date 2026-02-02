//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "Async/AsyncDemoPanel.h"
#include "Async/AsyncDemoVm.h"

// Helper macros for formatted text
#define IMGUI_TEXT_FMT(fmt_str, ...)                                           \
  ImGui::TextUnformatted(fmt::format(fmt_str, __VA_ARGS__).c_str())

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
  return 360.0F;
}

auto AsyncDemoPanel::GetIcon() const noexcept -> std::string_view
{
  // Use a generic settings icon or list icon
  return oxygen::imgui::icons::kIconDemoPanel;
}

auto AsyncDemoPanel::DrawContents() -> void
{
  if (!vm_)
    return;

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

    ImGui::Text("Animation Time: %.2F s", vm_->GetAnimationTime());
    ImGui::Text("Spheres: %zu", vm_->GetSphereCount());

    if (vm_->GetSphereCount() > 0) {
      if (ImGui::TreeNode("Sphere Details")) {
        for (size_t i = 0; i < vm_->GetSphereCount() && i < 5; ++i) {
          ImGui::TextUnformatted(vm_->GetSphereInfo(i).c_str());
        }
        if (vm_->GetSphereCount() > 5) {
          ImGui::TextDisabled("... and %zu more", vm_->GetSphereCount() - 5);
        }
        ImGui::TreePop();
      }
    }
  } else {
    if (open) {
      vm_->SetSceneSectionOpen(false);
    }
  }
}

void AsyncDemoPanel::DrawSpotlightControls()
{
  ImGui::SetNextItemOpen(vm_->GetSpotlightSectionOpen());
  if (ImGui::CollapsingHeader("Spotlight")) {
    vm_->SetSpotlightSectionOpen(true);

    if (!vm_->IsSpotlightAvailable()) {
      ImGui::TextColored(ImVec4(1, 1, 0, 1), "Spotlight not created yet.");
      if (ImGui::Button("Create Spotlight")) {
        vm_->EnsureSpotlight();
      }
    } else {
      bool enabled = vm_->GetSpotlightEnabled();
      if (ImGui::Checkbox("Enabled", &enabled)) {
        vm_->SetSpotlightEnabled(enabled);
      }

      bool shadows = vm_->GetSpotlightCastsShadows();
      if (ImGui::Checkbox("Cast Shadows", &shadows)) {
        vm_->SetSpotlightCastsShadows(shadows);
      }

      float intensity = vm_->GetSpotlightIntensity();
      if (ImGui::SliderFloat("Intensity", &intensity, 0.0F, 1000.0F)) {
        vm_->SetSpotlightIntensity(intensity);
      }

      float range = vm_->GetSpotlightRange();
      if (ImGui::SliderFloat("Range", &range, 1.0F, 100.0F)) {
        vm_->SetSpotlightRange(range);
      }

      // Cones
      float inner = glm::degrees(vm_->GetSpotlightInnerCone());
      float outer = glm::degrees(vm_->GetSpotlightOuterCone());

      bool changed = false;
      if (ImGui::SliderFloat("Inner Cone", &inner, 1.0F, 89.0F, "%.1F deg")) {
        if (inner > outer)
          inner = outer;
        changed = true;
        vm_->SetSpotlightInnerCone(glm::radians(inner));
      }
      if (ImGui::SliderFloat("Outer Cone", &outer, 1.0F, 89.0F, "%.1F deg")) {
        if (outer < inner)
          outer = inner;
        changed = true;
        vm_->SetSpotlightOuterCone(glm::radians(outer));
      }
    }
  } else {
    vm_->SetSpotlightSectionOpen(false);
  }
}

void AsyncDemoPanel::DrawProfilingInfo()
{
  ImGui::SetNextItemOpen(vm_->GetProfilerSectionOpen());
  if (ImGui::CollapsingHeader("Frame Profiling")) {
    vm_->SetProfilerSectionOpen(true);

    const auto& actions = vm_->GetFrameActions();
    if (!actions.empty()) {
      ImGui::Text("Frame Actions:");
      ImGui::BeginChild("ActionLog", ImVec2(0, 100), true);
      for (const auto& action : actions) {
        ImGui::TextUnformatted(action.c_str());
      }
      ImGui::EndChild();
    }

    const auto& timings = vm_->GetPhaseTimings();
    if (!timings.empty()) {
      ImGui::NewLine();
      ImGui::Text("Phase Timings:");
      if (ImGui::BeginTable(
            "Timings", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Phase");
        ImGui::TableSetupColumn("Duration (us)");
        ImGui::TableHeadersRow();

        for (const auto& [phase, duration] : timings) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(phase.c_str());
          ImGui::TableNextColumn();
          IMGUI_TEXT_FMT("{}", duration.count());
        }
        ImGui::EndTable();
      }
    }
  } else {
    vm_->SetProfilerSectionOpen(false);
  }
}

} // namespace oxygen::examples::async
