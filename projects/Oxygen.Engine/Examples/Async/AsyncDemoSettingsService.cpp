//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>

#include "Async/AsyncDemoSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>

namespace oxygen::examples::async {
namespace {
  constexpr float kDefaultSpotlightFluxLm = 3000.0F;
  constexpr float kDefaultSpotlightRangeMeters = 60.0F;
  constexpr float kDefaultSpotlightInnerConeDegrees = 12.0F;
  constexpr float kDefaultSpotlightOuterConeDegrees = 26.0F;
} // namespace

auto AsyncDemoSettingsService::GetSceneSectionOpen() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kSceneOpenKey).value_or(true);
}

auto AsyncDemoSettingsService::SetSceneSectionOpen(bool open) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kSceneOpenKey, open);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightSectionOpen() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kSpotlightOpenKey).value_or(true);
}

auto AsyncDemoSettingsService::SetSpotlightSectionOpen(bool open) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kSpotlightOpenKey, open);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetProfilerSectionOpen() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kProfilerOpenKey).value_or(true);
}

auto AsyncDemoSettingsService::SetProfilerSectionOpen(bool open) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kProfilerOpenKey, open);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightIntensity() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kSpotlightIntensityKey)
    .value_or(kDefaultSpotlightFluxLm);
}

auto AsyncDemoSettingsService::SetSpotlightIntensity(float intensity) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpotlightIntensityKey, intensity);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightRange() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kSpotlightRangeKey)
    .value_or(kDefaultSpotlightRangeMeters);
}

auto AsyncDemoSettingsService::SetSpotlightRange(float range) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpotlightRangeKey, range);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightColor() const -> glm::vec3
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float r = settings->GetFloat(kSpotlightColorRKey).value_or(1.0F);
  const float g = settings->GetFloat(kSpotlightColorGKey).value_or(1.0F);
  const float b = settings->GetFloat(kSpotlightColorBKey).value_or(1.0F);
  return { r, g, b };
}

auto AsyncDemoSettingsService::SetSpotlightColor(glm::vec3 color) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpotlightColorRKey, color.r);
  settings->SetFloat(kSpotlightColorGKey, color.g);
  settings->SetFloat(kSpotlightColorBKey, color.b);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightInnerCone() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kSpotlightInnerConeKey)
    .value_or(glm::radians(kDefaultSpotlightInnerConeDegrees));
}

auto AsyncDemoSettingsService::SetSpotlightInnerCone(float angle_rad) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpotlightInnerConeKey, angle_rad);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightOuterCone() const -> float
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetFloat(kSpotlightOuterConeKey)
    .value_or(glm::radians(kDefaultSpotlightOuterConeDegrees));
}

auto AsyncDemoSettingsService::SetSpotlightOuterCone(float angle_rad) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetFloat(kSpotlightOuterConeKey, angle_rad);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightEnabled() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kSpotlightEnabledKey).value_or(true);
}

auto AsyncDemoSettingsService::SetSpotlightEnabled(bool enabled) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kSpotlightEnabledKey, enabled);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetSpotlightCastsShadows() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  return settings->GetBool(kSpotlightShadowsKey).value_or(true);
}

auto AsyncDemoSettingsService::SetSpotlightCastsShadows(bool casts_shadows)
  -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  settings->SetBool(kSpotlightShadowsKey, casts_shadows);
  ++epoch_;
}

auto AsyncDemoSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

} // namespace oxygen::examples::async
