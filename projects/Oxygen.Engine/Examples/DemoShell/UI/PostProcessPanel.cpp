//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Runtime/SceneActivationPolicy.h"
#include "DemoShell/UI/PostProcessPanel.h"
#include "DemoShell/UI/PostProcessVm.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace oxygen::examples::ui {

namespace {

  constexpr auto kModes = std::array {
    engine::ExposureMode::kManual,
    engine::ExposureMode::kManualCamera,
    engine::ExposureMode::kAuto,
  };
  constexpr auto kModeNames = std::array { "manual", "camera", "auto" };
  constexpr auto kMeterings = std::array {
    engine::MeteringMode::kAverage,
    engine::MeteringMode::kCenterWeighted,
    engine::MeteringMode::kSpot,
  };
  constexpr auto kMeteringNames = std::array { "average", "center", "spot" };
  constexpr auto kToneCurveNames
    = std::array { "none", "aces", "filmic", "reinhard" };

  auto ToneCurveIndex(const engine::ToneMapper mode) -> std::size_t
  {
    switch (mode) {
    case engine::ToneMapper::kAcesFitted:
      return 1U;
    case engine::ToneMapper::kFilmic:
      return 2U;
    case engine::ToneMapper::kReinhard:
      return 3U;
    case engine::ToneMapper::kNone:
      break;
    }
    return 0U;
  }

  template <typename T, std::size_t N>
  auto NameOf(const std::array<T, N>& values,
    const std::array<const char*, N>& names, const T value) -> const char*
  {
    for (std::size_t i = 0; i < N; ++i) {
      if (values.at(i) == value) {
        return names.at(i);
      }
    }
    return names.front();
  }

  //! `metered:added` pairs separated by spaces, ordered by metered EV100.
  auto FormatCurve(const std::vector<scene::ExposureCompensationKey>& keys)
    -> std::string
  {
    std::string text;
    for (const auto& key : keys) {
      if (!text.empty()) {
        text += ' ';
      }
      text += std::to_string(key.metered_ev) + ':'
        + std::to_string(key.compensation_ev);
    }
    return text;
  }

} // namespace

PostProcessPanel::PostProcessPanel(
  observer_ptr<PostProcessVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "PostProcessPanel requires PostProcessVm");
  DCHECK_NOTNULL_F(commands_, "PostProcessPanel requires PanelCommands");
}

auto PostProcessPanel::RunOnTarget(
  const std::string_view command, nlohmann::json arguments) const -> void
{
  // Without a scene there is nothing to edit; the controls show defaults.
  if (const auto target = vm_->GetAuthoringTarget()) {
    arguments["target"] = *target;
    commands_->Run(command, arguments);
  }
}

auto PostProcessPanel::RunExposure(nlohmann::json fields) const -> void
{
  RunOnTarget("pp.exposure", std::move(fields));
}

auto PostProcessPanel::RunCamera(
  const float aperture, const float shutter_rate, const float iso) const -> void
{
  RunOnTarget("pp.camera",
    {
      { "aperture_f", aperture },
      { "shutter_rate", shutter_rate },
      { "iso", iso },
    });
}

auto PostProcessPanel::RunOutput(
  const std::string_view tone_curve, const float gamma) const -> void
{
  RunOnTarget(
    "pp.output", { { "tone_curve", tone_curve }, { "gamma", gamma } });
}

auto PostProcessPanel::GetName() const noexcept -> std::string_view
{
  return "Post Process";
}

auto PostProcessPanel::GetPreferredWidth() const noexcept -> float
{
  constexpr float kPreferredPanelWidth = 360.0F;
  return kPreferredPanelWidth;
}

auto PostProcessPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconHdrTonemap;
}

auto PostProcessPanel::DrawContents() -> void
{
  const auto scene_scope = std::to_string(vm_->GetSceneRevision());
  ImGui::PushID(scene_scope.c_str());
  ImGui::PushTextWrapPos(0.0F);
  if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawExposureSection();
  }

  if (ImGui::CollapsingHeader("Tonemapping", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawTonemappingSection();
  }
  ImGui::PopTextWrapPos();
  ImGui::PopID();
}

namespace {
  constexpr float kPercentScale = 100.0F;

  struct FloatDragOptions {
    float step {};
    float minimum {};
    float maximum {};
    const char* format { "%.3g" };
  };

  // Mouse-drag ranges are ergonomic defaults. Typed values are validated by
  // the settings service against the complete exposure contract.
  constexpr auto kEvDrag
    = FloatDragOptions { .step = 0.1F, .minimum = -16.0F, .maximum = 24.0F };
  constexpr auto kApertureDrag
    = FloatDragOptions { .step = 0.1F, .minimum = 0.1F, .maximum = 64.0F };
  constexpr auto kShutterDrag
    = FloatDragOptions { .step = 1.0F, .minimum = 1.0F, .maximum = 8000.0F };
  constexpr auto kIsoDrag
    = FloatDragOptions { .step = 10.0F, .minimum = 1.0F, .maximum = 12800.0F };
  constexpr auto kCompensationDrag
    = FloatDragOptions { .step = 0.1F, .minimum = -10.0F, .maximum = 10.0F };
  constexpr auto kAdaptationRateDrag
    = FloatDragOptions { .step = 0.1F, .minimum = 0.0F, .maximum = 20.0F };
  constexpr auto kSpotRadiusDrag
    = FloatDragOptions { .step = 0.005F, .minimum = 0.0F, .maximum = 1.0F };
  constexpr auto kCalibrationDrag
    = FloatDragOptions { .step = 0.1F, .minimum = 0.1F, .maximum = 25.0F };
  constexpr auto kLuminanceDrag
    = FloatDragOptions { .step = 0.01F, .minimum = 0.0F, .maximum = 1.0F };
  constexpr auto kPercentDrag = FloatDragOptions {
    .step = 1.0F,
    .minimum = 0.0F,
    .maximum = kPercentScale,
    .format = "%.1f",
  };
  constexpr auto kTransitionDrag
    = FloatDragOptions { .step = 0.1F, .minimum = 0.01F, .maximum = 10.0F };
  constexpr auto kGammaDrag = FloatDragOptions {
    .step = 0.05F,
    .minimum = engine::kMinDisplayGamma,
    .maximum = 3.0F,
  };

  template <size_t Count, typename Draw>
  auto NumericControl(
    const char* id, std::array<float, Count>& values, const Draw& draw) -> bool
  {
    ImGui::PushID(id);
    auto* storage = ImGui::GetStateStorage();
    const auto editing_id = ImGui::GetID("editing");
    const bool was_editing = storage->GetBool(editing_id);
    auto draft = values;
    for (size_t index = 0; index < Count; ++index) {
      ImGui::PushID(static_cast<int>(index));
      if (was_editing) {
        draft.at(index)
          = storage->GetFloat(ImGui::GetID("draft"), values.at(index));
      }
      ImGui::PopID();
    }

    const bool changed = draw(draft);
    const bool editing = ImGui::IsItemActive() && ImGui::GetIO().WantTextInput;
    const bool cancelled = was_editing && ImGui::IsKeyPressed(ImGuiKey_Escape);
    storage->SetBool(editing_id, editing && !cancelled);
    for (size_t index = 0; index < Count; ++index) {
      ImGui::PushID(static_cast<int>(index));
      storage->SetFloat(ImGui::GetID("draft"), draft.at(index));
      ImGui::PopID();
    }
    ImGui::PopID();

    // Preserve text drafts across frames so Tab and focus loss commit the
    // same value as Enter. Escape cancels; pointer drags still update live.
    if (editing || cancelled || (!changed && !was_editing) || draft == values) {
      return false;
    }
    values = draft;
    return true;
  }

  auto FloatControl(const char* label, float& value,
    const FloatDragOptions& options, const char* help) -> bool
  {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", help);
    }
    ImGui::SetNextItemWidth(-1.0F);
    auto values = std::array { value };
    const bool changed
      = NumericControl("value", values, [&options](auto& draft) -> bool {
          return ImGui::DragFloat("##value", &draft.front(), options.step,
            options.minimum, options.maximum, options.format);
        });
    value = values.front();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", help);
    }
    ImGui::PopID();
    return changed;
  }

  auto ExposureModeLabel(const engine::ExposureMode mode) -> const char*
  {
    switch (mode) {
    case engine::ExposureMode::kManual:
      return "Manual EV100";
    case engine::ExposureMode::kManualCamera:
      return "Physical camera";
    case engine::ExposureMode::kAuto:
      return "Automatic";
    }
    return "Unknown";
  }

  auto CurveIsOrdered(const std::vector<scene::ExposureCompensationKey>& keys)
    -> bool
  {
    auto previous = -std::numeric_limits<float>::infinity();
    for (const auto& key : keys) {
      if (!std::isfinite(key.metered_ev) || !std::isfinite(key.compensation_ev)
        || key.metered_ev <= previous) {
        return false;
      }
      previous = key.metered_ev;
    }
    return true;
  }

  auto DrawCurvePreview(const std::vector<scene::ExposureCompensationKey>& keys)
    -> void
  {
    if (keys.empty()) {
      return;
    }
    const auto size = ImVec2(
      ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 4.0F);
    ImGui::InvisibleButton("Curve preview", size);
    const auto minimum = ImGui::GetItemRectMin();
    const auto maximum = ImGui::GetItemRectMax();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg),
      ImGui::GetStyle().FrameRounding);
    const auto padding = ImGui::GetStyle().FramePadding;
    if (!CurveIsOrdered(keys)) {
      draw->AddText(ImVec2(minimum.x + padding.x, minimum.y + padding.y),
        ImGui::GetColorU32(ImGuiCol_TextDisabled), "Preview unavailable");
      return;
    }
    double min_ev = keys.front().metered_ev;
    double max_ev = keys.back().metered_ev;
    constexpr double kConstantCurvePaddingFraction = 0.1;
    if (min_ev == max_ev) {
      const double margin
        = (std::max)(1.0, std::abs(min_ev) * kConstantCurvePaddingFraction);
      min_ev -= margin;
      max_ev += margin;
    }
    auto min_compensation = static_cast<double>(keys.front().compensation_ev);
    auto max_compensation = min_compensation;
    for (const auto& key : keys) {
      min_compensation = (std::min)(min_compensation,
        static_cast<double>(key.compensation_ev));
      max_compensation = (std::max)(max_compensation,
        static_cast<double>(key.compensation_ev));
    }
    if (min_compensation == max_compensation) {
      const double margin = (std::max)(1.0,
        std::abs(min_compensation) * kConstantCurvePaddingFraction);
      min_compensation -= margin;
      max_compensation += margin;
    }
    const auto point
      = [&](const scene::ExposureCompensationKey& key) -> ImVec2 {
      const auto x = static_cast<float>(
        (static_cast<double>(key.metered_ev) - min_ev) / (max_ev - min_ev));
      const auto y = static_cast<float>(
        (static_cast<double>(key.compensation_ev) - min_compensation)
        / (max_compensation - min_compensation));
      return { minimum.x + padding.x + (x * (size.x - (2.0F * padding.x))),
        maximum.y - padding.y - (y * (size.y - (2.0F * padding.y))) };
    };
    auto previous = ImVec2(minimum.x + padding.x, point(keys.front()).y);
    for (const auto& key : keys) {
      const auto current = point(key);
      draw->AddLine(
        previous, current, ImGui::GetColorU32(ImGuiCol_PlotLines), 2.0F);
      constexpr float kCurveMarkerRadius = 3.0F;
      draw->AddCircleFilled(
        current, kCurveMarkerRadius, ImGui::GetColorU32(ImGuiCol_PlotLines));
      previous = current;
    }
    draw->AddLine(previous, ImVec2(maximum.x - padding.x, previous.y),
      ImGui::GetColorU32(ImGuiCol_PlotLines), 2.0F);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Authored compensation curve\nHorizontal: metered "
                        "EV100\nVertical: added compensation (EV)");
    }
  }
} // namespace

void PostProcessPanel::DrawExposureSection()
{
  const auto status = vm_->GetExposureStatus();
  const bool experiment_owned = vm_->GetSceneActivationPolicy()
    == SceneActivationPolicy::kExperimentOwned;
  ImGui::TextDisabled("%s",
    experiment_owned ? "Experiment controls - changes are temporary"
                     : "Demo controls - changes are saved");
  const bool externally_owned
    = status && (status->uses_view_override || status->shared_source);
  if (externally_owned) {
    ImGui::TextWrapped("%s",
      status->shared_source
        ? "Exposure follows another view. Edit its source view."
        : "Exposure is controlled by this view's override.");
  }
  ImGui::BeginDisabled(externally_owned);
  bool enabled = vm_->GetExposureEnabled();
  if (ImGui::Checkbox("Enable exposure", &enabled)) {
    RunExposure({ { "enabled", enabled } });
  }
  ImGui::BeginDisabled(!enabled);
  auto mode = vm_->GetExposureMode();
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::BeginCombo("##Exposure mode", ExposureModeLabel(mode))) {
    for (const auto candidate : {
           engine::ExposureMode::kManual,
           engine::ExposureMode::kManualCamera,
           engine::ExposureMode::kAuto,
         }) {
      if (ImGui::Selectable(ExposureModeLabel(candidate), mode == candidate)) {
        RunExposure({ { "mode", NameOf(kModes, kModeNames, candidate) } });
      }
    }
    ImGui::EndCombo();
  }
  if (mode == engine::ExposureMode::kManual) {
    auto ev = vm_->GetManualExposureEv();
    if (FloatControl(
          "EV100", ev, kEvDrag, "Increasing EV100 by one halves exposure.")) {
      RunExposure({ { "manual_ev", ev } });
    }
  } else if (mode == engine::ExposureMode::kManualCamera) {
    const bool has_camera = vm_->HasActiveCamera();
    ImGui::BeginDisabled(!has_camera);
    auto aperture = vm_->GetManualCameraAperture();
    auto shutter = vm_->GetManualCameraShutterRate();
    auto iso = vm_->GetManualCameraIso();
    if (FloatControl("Aperture (f-number)", aperture, kApertureDrag,
          "A smaller f-number increases exposure.")) {
      RunCamera(aperture, shutter, iso);
    }
    if (FloatControl("Shutter rate (1/s)", shutter, kShutterDrag,
          "125 means a shutter time of 1/125 second.")) {
      RunCamera(aperture, shutter, iso);
    }
    if (FloatControl("ISO", iso, kIsoDrag, "Higher ISO increases exposure.")) {
      RunCamera(aperture, shutter, iso);
    }
    ImGui::EndDisabled();
    if (has_camera) {
      ImGui::Text("Camera EV100: %.2f", vm_->GetManualCameraEv());
    } else {
      ImGui::TextWrapped("Select an active camera to use physical exposure.");
    }
  }
  auto compensation = vm_->GetExposureCompensation();
  if (FloatControl("Compensation (EV)", compensation, kCompensationDrag,
        "Positive compensation brightens the image. +1 EV doubles exposure.")) {
    RunExposure({ { "compensation_ev", compensation } });
  }
  if (mode == engine::ExposureMode::kAuto) {
    DrawAutoExposureControls();
  }
  if (ImGui::TreeNode("Advanced exposure")) {
    DrawAdvancedExposureControls();
    ImGui::TreePop();
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  DrawExposureStatus(status);
}

void PostProcessPanel::DrawAutoExposureControls()
{
  auto minimum = vm_->GetAutoExposureMinEv();
  auto maximum = vm_->GetAutoExposureMaxEv();
  ImGui::TextUnformatted("Metering limits (EV100)");
  ImGui::SetNextItemWidth(-1.0F);
  auto limits = std::array { minimum, maximum };
  if (NumericControl("EV limits", limits, [](auto& draft) -> bool {
        return ImGui::DragFloatRange2("##value", &draft.front(), &draft.back(),
          kEvDrag.step, kEvDrag.minimum, kEvDrag.maximum, "Min %.2f",
          "Max %.2f");
      })) {
    RunExposure({ { "min_ev", limits.front() }, { "max_ev", limits.back() } });
  }
  if (minimum == maximum) {
    ImGui::TextDisabled("Locked to a fixed metered EV100");
  }
  auto speed_up = vm_->GetAutoExposureAdaptationSpeedUp();
  auto speed_down = vm_->GetAutoExposureAdaptationSpeedDown();
  if (FloatControl("To bright scenes (EV/s)", speed_up, kAdaptationRateDrag,
        "Speed when moving from dark to bright scenes. Zero holds exposure in "
        "this direction.")) {
    RunExposure({ { "speed_up", speed_up } });
  }
  if (FloatControl("To dark scenes (EV/s)", speed_down, kAdaptationRateDrag,
        "Speed when moving from bright to dark scenes. Zero holds exposure in "
        "this direction.")) {
    RunExposure({ { "speed_down", speed_down } });
  }
  auto metering = vm_->GetAutoExposureMeteringMode();
  ImGui::TextUnformatted("Metering");
  ImGui::SetNextItemWidth(-1.0F);
  const auto preview = std::string(engine::to_string(metering));
  if (ImGui::BeginCombo("##Metering", preview.c_str())) {
    for (const auto candidate : {
           engine::MeteringMode::kAverage,
           engine::MeteringMode::kCenterWeighted,
           engine::MeteringMode::kSpot,
         }) {
      const auto label = std::string(engine::to_string(candidate));
      if (ImGui::Selectable(label.c_str(), metering == candidate)) {
        RunExposure({
          { "metering", NameOf(kMeterings, kMeteringNames, candidate) },
        });
        metering = candidate;
      }
    }
    ImGui::EndCombo();
  }
  if (metering == engine::MeteringMode::kSpot) {
    auto radius = vm_->GetAutoExposureSpotMeterRadius();
    if (FloatControl("Spot radius", radius, kSpotRadiusDrag,
          "Radius in normalized image coordinates. Zero selects only exact "
          "center samples.")) {
      RunExposure({ { "spot_meter_radius", radius } });
    }
  }
}

void PostProcessPanel::DrawAdvancedExposureControls()
{
  auto key = vm_->GetExposureKey();
  if (FloatControl("Calibration key", key, kCalibrationDrag,
        "Display calibration multiplier. The calibrated reference is 12.5.")) {
    RunExposure({ { "key", key } });
  }
  if (vm_->GetExposureMode() != engine::ExposureMode::kAuto) {
    return;
  }
  auto requested = vm_->GetExposureSettings();
  auto low = requested.low_percentile * kPercentScale;
  auto high = requested.high_percentile * kPercentScale;
  ImGui::TextUnformatted("Histogram percentiles (%)");
  ImGui::SetNextItemWidth(-1.0F);
  auto percentiles = std::array { low, high };
  if (NumericControl("Percentiles", percentiles, [](auto& draft) -> bool {
        return ImGui::DragFloatRange2("##value", &draft.front(), &draft.back(),
          0.5F, 0.0F, kPercentScale, "Low %.1f", "High %.1f");
      })) {
    RunExposure({
      { "low_percentile", percentiles.front() / kPercentScale },
      { "high_percentile", percentiles.back() / kPercentScale },
    });
  }
  auto minimum = requested.min_log_luminance;
  auto maximum = requested.min_log_luminance + requested.log_luminance_range;
  ImGui::TextUnformatted("Histogram bounds (log2 luminance)");
  ImGui::SetNextItemWidth(-1.0F);
  auto histogram_bounds = std::array { minimum, maximum };
  if (NumericControl(
        "Histogram bounds", histogram_bounds, [](auto& draft) -> bool {
          return ImGui::DragFloatRange2("##value", &draft.front(),
            &draft.back(), kEvDrag.step, engine::kMinExposureLogLuminance,
            engine::kMaxExposureLogLuminance, "Min %.1f", "Max %.1f");
        })) {
    RunExposure({
      { "min_log_luminance", histogram_bounds.front() },
      {
        "log_luminance_range",
        histogram_bounds.back() - histogram_bounds.front(),
      },
    });
  }
  auto target = requested.target_luminance;
  if (FloatControl("Target luminance", target, kLuminanceDrag,
        "Reference luminance for automatic exposure. Zero intentionally "
        "produces zero exposure.")) {
    RunExposure({ { "target_luminance", target } });
  }
  auto black = requested.black_influence * kPercentScale;
  if (FloatControl("Black-pixel influence (%)", black, kPercentDrag,
        "Weight given to black samples. Zero excludes them; 100 gives them "
        "full weight.")) {
    RunExposure({ { "black_influence", black / kPercentScale } });
  }
  auto distance = requested.transition_distance;
  if (FloatControl("Adaptation transition (EV)", distance, kTransitionDrag,
        "Distance from the target where adaptation changes from a constant "
        "rate to a smooth approach. Must be positive.")) {
    RunExposure({ { "transition_distance", distance } });
  }
  if (vm_->HasSceneMeteringMask()) {
    bool use_mask = vm_->GetUseSceneMeteringMask();
    if (ImGui::Checkbox("Use scene mask", &use_mask)) {
      RunExposure({ { "mask", use_mask ? "scene" : "off" } });
    }
  } else {
    ImGui::TextDisabled("No metering mask assigned by the scene");
  }
  if (ImGui::TreeNode("Compensation curve")) {
    DrawCompensationCurve(requested);
    ImGui::TreePop();
  }
  if (ImGui::Button("Reset auto controls")) {
    RunOnTarget("pp.reset_auto", nlohmann::json::object());
    curve_dirty_ = false;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset auto controls only. Mode, compensation, "
                      "calibration and camera settings are preserved.");
  }
}

void PostProcessPanel::DrawCompensationCurve(
  const scene::ExposureSettings& requested)
{
  const auto scene_revision = vm_->GetSceneRevision();
  const auto epoch = vm_->GetEpoch();
  if (!curve_initialized_ || scene_revision != curve_scene_revision_
    || (!curve_dirty_ && epoch != curve_epoch_)) {
    curve_draft_ = requested.compensation_curve;
    curve_scene_revision_ = scene_revision;
    curve_epoch_ = epoch;
    curve_initialized_ = true;
    curve_dirty_ = false;
  }
  ImGui::TextWrapped("Metered EV100 determines the added compensation. Empty "
                     "means no additional compensation.");
  DrawCurvePreview(curve_draft_);
  if (ImGui::BeginTable("Curve keys", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("EV100", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn(
      "Added EV", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    auto remove = std::optional<size_t> {};
    for (size_t index = 0; index < curve_draft_.size(); ++index) {
      auto& point = curve_draft_.at(index);
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      curve_dirty_
        |= ImGui::InputFloat("##EV100", &point.metered_ev, 0.0F, 0.0F, "%.4g");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      curve_dirty_ |= ImGui::InputFloat(
        "##Added EV", &point.compensation_ev, 0.0F, 0.0F, "%.4g");
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Remove")) {
        remove = index;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove) {
      curve_draft_.erase(
        std::next(curve_draft_.begin(), static_cast<std::ptrdiff_t>(*remove)));
      curve_dirty_ = true;
    }
  }
  const auto next_ev
    = curve_draft_.empty() ? 0.0F : curve_draft_.back().metered_ev + 1.0F;
  const bool can_add
    = curve_draft_.size() < engine::kMaxExposureCompensationCurveKeys
    && std::isfinite(next_ev)
    && (curve_draft_.empty() || next_ev > curve_draft_.back().metered_ev);
  ImGui::BeginDisabled(!can_add);
  if (ImGui::Button("Add key")) {
    curve_draft_.push_back({ .metered_ev = next_ev, .compensation_ev = 0.0F });
    curve_dirty_ = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool finite_keys
    = std::ranges::all_of(curve_draft_, [](const auto& point) -> bool {
        return std::isfinite(point.metered_ev)
          && std::isfinite(point.compensation_ev);
      });
  ImGui::BeginDisabled(!finite_keys || curve_draft_.size() < 2U);
  if (ImGui::Button("Sort by EV100")) {
    std::ranges::sort(
      curve_draft_, {}, &scene::ExposureCompensationKey::metered_ev);
    curve_dirty_ = true;
  }
  ImGui::EndDisabled();
  if (!CurveIsOrdered(curve_draft_)) {
    ImGui::TextWrapped(
      "Keys must be finite and ordered by EV100, with no duplicates.");
  }
  ImGui::BeginDisabled(!curve_dirty_ || !CurveIsOrdered(curve_draft_));
  if (ImGui::Button("Apply curve")) {
    // The draft resyncs from the applied settings once the command ran; a
    // rejected curve leaves the settings and reports the validation error.
    const auto keys = FormatCurve(curve_draft_);
    RunOnTarget("pp.curve", { { "keys", keys.empty() ? "clear" : keys } });
    curve_dirty_ = false;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!curve_dirty_);
  if (ImGui::Button("Discard edits")) {
    curve_draft_ = vm_->GetExposureSettings().compensation_curve;
    curve_dirty_ = false;
    curve_epoch_ = vm_->GetEpoch();
  }
  ImGui::EndDisabled();
}

void PostProcessPanel::DrawExposureStatus(
  const std::optional<vortex::ExposureSettingsStatus>& status)
{
  const auto error = vm_->GetValidationError();
  if (!error.empty()) {
    ImGui::TextWrapped("Edit not applied: %s", error.c_str());
  }
  if (!status) {
    ImGui::TextDisabled("Waiting for a rendered view");
    return;
  }
  if (status->settings_error) {
    const auto reason = scene::to_string(*status->settings_error);
    ImGui::TextWrapped("Renderer rejected the request: %.*s",
      static_cast<int>(reason.size()), reason.data());
  }
  if (status->mask_status == vortex::ExposureMaskStatus::kPending) {
    ImGui::TextWrapped("%s",
      status->active_settings
        ? "Loading metering mask. Previous accepted settings remain in use."
        : "Loading metering mask before applying these settings.");
  } else if (status->mask_status == vortex::ExposureMaskStatus::kFailed) {
    ImGui::TextWrapped("Metering mask unavailable: %s. Disable the mask or "
                       "reload the scene after fixing the texture.",
      status->mask_error.c_str());
    if (ImGui::Button("Disable mask")) {
      RunExposure({ { "mask", "off" } });
    }
  }
  if (status->active_settings) {
    const auto& active = *status->active_settings;
    if (active != vm_->GetExposureSettings()) {
      ImGui::Text("Accepted mode: %s",
        active.enabled ? ExposureModeLabel(active.mode) : "Exposure off");
      if (active.enabled && active.mode == engine::ExposureMode::kManual) {
        ImGui::Text("Accepted EV100: %.2f", active.manual_ev);
      }
      ImGui::Text("Accepted compensation: %+.2f EV", active.compensation_ev);
    } else if (!status->settings_error
      && status->mask_status != vortex::ExposureMaskStatus::kPending
      && status->mask_status != vortex::ExposureMaskStatus::kFailed) {
      ImGui::TextDisabled("Active settings accepted");
    }
  } else {
    ImGui::TextWrapped("No settings revision has been accepted yet.");
  }
  if (status->metering_input_failed.value_or(false)) {
    ImGui::TextWrapped(
      "Metering input failed validation. The last valid exposure is retained.");
  }
}

void PostProcessPanel::DrawTonemappingSection()
{
  using engine::ToneMapper;

  ToneMapper current_mode
    = vm_->GetTonemappingEnabled() ? vm_->GetToneMapper() : ToneMapper::kNone;
  const auto* mode_str = "Unknown";
  switch (current_mode) {
  case ToneMapper::kAcesFitted:
    mode_str = "ACES";
    break;
  case ToneMapper::kFilmic:
    mode_str = "Filmic";
    break;
  case ToneMapper::kReinhard:
    mode_str = "Reinhard";
    break;
  case ToneMapper::kNone:
    mode_str = "None";
    break;
  }

  ImGui::TextUnformatted("Tone curve");
  ImGui::SetNextItemWidth(-1.0F);
  if (ImGui::BeginCombo("##Tone curve", mode_str)) {
    if (ImGui::Selectable("None", current_mode == ToneMapper::kNone)) {
      RunOutput("none", vm_->GetGamma());
    }
    if (ImGui::Selectable("ACES", current_mode == ToneMapper::kAcesFitted)) {
      RunOutput("aces", vm_->GetGamma());
    }
    if (ImGui::Selectable("Filmic", current_mode == ToneMapper::kFilmic)) {
      RunOutput("filmic", vm_->GetGamma());
    }
    if (ImGui::Selectable("Reinhard", current_mode == ToneMapper::kReinhard)) {
      RunOutput("reinhard", vm_->GetGamma());
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Choose how HDR brightness maps to the display. None "
                      "leaves the tone curve neutral.");
  }

  float gamma = vm_->GetGamma();
  if (FloatControl("Display gamma", gamma, kGammaDrag,
        "Display gamma applied after the selected tone curve.")) {
    RunOutput(kToneCurveNames.at(ToneCurveIndex(current_mode)), gamma);
  }
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
