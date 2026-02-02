//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Runtime/RenderingPipeline.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Logging.h>

namespace oxygen::examples::ui {

auto PostProcessSettingsService::Initialize(
  observer_ptr<RenderingPipeline> pipeline) -> void
{
  DCHECK_NOTNULL_F(pipeline);
  pipeline_ = pipeline;

  // Push initial state
  if (pipeline_) {
    pipeline_->SetExposureMode(GetExposureMode());
    pipeline_->SetExposureValue(GetManualExposureEV100());
    pipeline_->SetToneMapper(
      GetTonemappingEnabled() ? GetToneMapper() : engine::ToneMapper::kNone);
  }
}

auto PostProcessSettingsService::GetCompositingEnabled() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kCompositingEnabledKey).value_or(true);
  }
  return true;
}

auto PostProcessSettingsService::SetCompositingEnabled(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kCompositingEnabledKey, enabled);
    epoch_++;
  }
}

auto PostProcessSettingsService::GetCompositingAlpha() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kCompositingAlphaKey).value_or(1.0F);
  }
  return 1.0F;
}

auto PostProcessSettingsService::SetCompositingAlpha(float alpha) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kCompositingAlphaKey, alpha);
    epoch_++;
  }
}

// Exposure

auto PostProcessSettingsService::GetExposureMode() const -> engine::ExposureMode
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kExposureModeKey).value_or("manual");
    if (val == "auto")
      return engine::ExposureMode::kAuto;
  }
  return engine::ExposureMode::kManual;
}

auto PostProcessSettingsService::SetExposureMode(engine::ExposureMode mode)
  -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kExposureModeKey, engine::to_string(mode));
    epoch_++;

    if (pipeline_) {
      pipeline_->SetExposureMode(mode);
    }
  }
}

auto PostProcessSettingsService::GetManualExposureEV100() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kExposureManualEV100Key).value_or(9.7F);
  }
  return 9.7F;
}

auto PostProcessSettingsService::SetManualExposureEV100(float ev100) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kExposureManualEV100Key, ev100);
    epoch_++;

    if (pipeline_) {
      pipeline_->SetExposureValue(ev100);
    }
  }
}

auto PostProcessSettingsService::GetExposureCompensation() const -> float
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetFloat(kExposureCompensationKey).value_or(0.0F);
  }
  return 0.0F;
}

auto PostProcessSettingsService::SetExposureCompensation(float stops) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetFloat(kExposureCompensationKey, stops);
    epoch_++;
  }
}

// Tonemapping

auto PostProcessSettingsService::GetTonemappingEnabled() const -> bool
{
  const auto settings = ResolveSettings();
  if (settings) {
    return settings->GetBool(kTonemappingEnabledKey).value_or(true);
  }
  return true;
}

auto PostProcessSettingsService::SetTonemappingEnabled(bool enabled) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetBool(kTonemappingEnabledKey, enabled);
    epoch_++;

    if (pipeline_) {
      pipeline_->SetToneMapper(
        enabled ? GetToneMapper() : engine::ToneMapper::kNone);
    }
  }
}

auto PostProcessSettingsService::GetToneMapper() const -> engine::ToneMapper
{
  const auto settings = ResolveSettings();
  if (settings) {
    auto val = settings->GetString(kToneMapperKey).value_or("aces");
    if (val == "reinhard")
      return engine::ToneMapper::kReinhard;
    if (val == "aces")
      return engine::ToneMapper::kAcesFitted;
    if (val == "filmic")
      return engine::ToneMapper::kFilmic;
    if (val == "none")
      return engine::ToneMapper::kNone;
  }
  return engine::ToneMapper::kAcesFitted;
}

auto PostProcessSettingsService::SetToneMapper(engine::ToneMapper mode) -> void
{
  const auto settings = ResolveSettings();
  if (settings) {
    settings->SetString(kToneMapperKey, engine::to_string(mode));
    epoch_++;

    if (pipeline_ && GetTonemappingEnabled()) {
      pipeline_->SetToneMapper(mode);
    }
  }
}

auto PostProcessSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto PostProcessSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples::ui
