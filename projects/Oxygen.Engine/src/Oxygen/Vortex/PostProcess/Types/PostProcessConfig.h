//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <optional>
#include <utility>

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::vortex {

//! Authored post-process inputs. Exposure has one writable authority.
struct PostProcessConfig {
  scene::ExposureSettings exposure;
  engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
  bool enable_bloom { true };
  //! Transient diagnostic presentation; does not alter authored exposure.
  bool temporary_unit_exposure { false };
  float gamma { 2.2F };
  float bloom_intensity { 0.5F };
  float bloom_threshold { 1.0F };

  auto operator==(const PostProcessConfig&) const -> bool = default;
};

//! Explicit immutable input for passes that execute outside PostProcessService.
class ResolvedPostProcessConfig {
public:
  ResolvedPostProcessConfig()
    : exposure_(*scene::ResolveExposureSettings(config_.exposure))
  {
  }

  [[nodiscard]] static auto Resolve(const PostProcessConfig& config,
    std::optional<float> camera_ev = {}, std::uint64_t revision = 0U)
    -> std::expected<ResolvedPostProcessConfig, scene::ExposureSettingsError>
  {
    auto exposure = scene::ResolveExposureSettings(config.exposure, camera_ev);
    if (!exposure) {
      return std::unexpected(exposure.error());
    }
    return ResolvedPostProcessConfig(
      config, std::move(*exposure), revision, camera_ev);
  }

  [[nodiscard]] auto Settings() const noexcept -> const PostProcessConfig&
  {
    return config_;
  }
  [[nodiscard]] auto Exposure() const noexcept
    -> const scene::ResolvedExposureSettings&
  {
    return exposure_;
  }
  [[nodiscard]] auto Revision() const noexcept -> std::uint64_t
  {
    return revision_;
  }
  [[nodiscard]] auto CameraEv() const noexcept -> std::optional<float>
  {
    return camera_ev_;
  }
  [[nodiscard]] auto WithDiagnosticOverride(bool enabled) const
    -> ResolvedPostProcessConfig
  {
    auto result = *this;
    result.config_.temporary_unit_exposure = enabled;
    return result;
  }

private:
  friend class PostProcessService;
  ResolvedPostProcessConfig(PostProcessConfig config,
    scene::ResolvedExposureSettings exposure, std::uint64_t revision,
    std::optional<float> camera_ev = {})
    : config_(std::move(config))
    , exposure_(std::move(exposure))
    , revision_(revision)
    , camera_ev_(camera_ev)
  {
    config_.exposure = exposure_.authored;
  }
  PostProcessConfig config_;
  scene::ResolvedExposureSettings exposure_;
  std::uint64_t revision_ { 0U };
  std::optional<float> camera_ev_;
};

} // namespace oxygen::vortex
