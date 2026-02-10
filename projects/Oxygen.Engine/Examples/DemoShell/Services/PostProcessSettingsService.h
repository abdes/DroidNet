//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen {
namespace engine {
  enum class ExposureMode : std::uint32_t;
  enum class ToneMapper : std::uint32_t;
  enum class MeteringMode : std::uint32_t;
} // namespace engine
namespace scene {
  class Scene;
} // namespace scene

namespace examples {

  class CameraSettingsService;
  class RenderingPipeline;
  class SettingsService;

  namespace ui {

    //! Settings persistence for the PostProcessPanel.
    class PostProcessSettingsService {
    public:
      PostProcessSettingsService() = default;
      virtual ~PostProcessSettingsService() = default;

      OXYGEN_MAKE_NON_COPYABLE(PostProcessSettingsService)
      OXYGEN_MAKE_NON_MOVABLE(PostProcessSettingsService)

      //! Associates the service with a rendering pipeline and synchronizes
      //! initial state.
      virtual auto Initialize(observer_ptr<RenderingPipeline> pipeline) -> void;

      //! Binds the camera settings service used for camera exposure settings.
      virtual auto BindCameraSettings(
        observer_ptr<CameraSettingsService> camera_settings) -> void;

      //! Binds the active scene for post-process system updates.
      virtual auto BindScene(observer_ptr<scene::Scene> scene) -> void;

      // Exposure
      [[nodiscard]] virtual auto GetExposureEnabled() const -> bool;
      virtual auto SetExposureEnabled(bool enabled) -> void;

      [[nodiscard]] virtual auto GetExposureMode() const
        -> engine::ExposureMode;
      virtual auto SetExposureMode(engine::ExposureMode mode) -> void;

      [[nodiscard]] virtual auto GetManualExposureEv() const -> float;
      virtual auto SetManualExposureEv(float ev) -> void;

      [[nodiscard]] virtual auto GetManualCameraAperture() const -> float;
      virtual auto SetManualCameraAperture(float aperture) -> void;

      [[nodiscard]] virtual auto GetManualCameraShutterRate() const -> float;
      virtual auto SetManualCameraShutterRate(float shutter_rate) -> void;

      [[nodiscard]] virtual auto GetManualCameraIso() const -> float;
      virtual auto SetManualCameraIso(float iso) -> void;

      [[nodiscard]] virtual auto GetManualCameraEv() const -> float;

      [[nodiscard]] virtual auto GetExposureCompensation() const -> float;
      virtual auto SetExposureCompensation(float stops) -> void;

      [[nodiscard]] virtual auto GetExposureKey() const -> float;
      virtual auto SetExposureKey(float exposure_key) -> void;

      // Tonemapping
      [[nodiscard]] virtual auto GetTonemappingEnabled() const -> bool;
      virtual auto SetTonemappingEnabled(bool enabled) -> void;

      [[nodiscard]] virtual auto GetToneMapper() const -> engine::ToneMapper;
      virtual auto SetToneMapper(engine::ToneMapper mode) -> void;

      // Auto Exposure
      [[nodiscard]] virtual auto GetAutoExposureAdaptationSpeedUp() const
        -> float;
      virtual auto SetAutoExposureAdaptationSpeedUp(float speed) -> void;

      [[nodiscard]] virtual auto GetAutoExposureAdaptationSpeedDown() const
        -> float;
      virtual auto SetAutoExposureAdaptationSpeedDown(float speed) -> void;

      [[nodiscard]] virtual auto GetAutoExposureLowPercentile() const -> float;
      virtual auto SetAutoExposureLowPercentile(float percentile) -> void;

      [[nodiscard]] virtual auto GetAutoExposureHighPercentile() const -> float;
      virtual auto SetAutoExposureHighPercentile(float percentile) -> void;

      [[nodiscard]] virtual auto GetAutoExposureMinLogLuminance() const
        -> float;
      virtual auto SetAutoExposureMinLogLuminance(float luminance) -> void;

      [[nodiscard]] virtual auto GetAutoExposureLogLuminanceRange() const
        -> float;
      virtual auto SetAutoExposureLogLuminanceRange(float range) -> void;

      [[nodiscard]] virtual auto GetAutoExposureTargetLuminance() const
        -> float;
      virtual auto SetAutoExposureTargetLuminance(float luminance) -> void;

      [[nodiscard]] virtual auto GetAutoExposureMeteringMode() const
        -> engine::MeteringMode;
      virtual auto SetAutoExposureMeteringMode(engine::MeteringMode mode)
        -> void;

      //! Resets all post-process settings to their default values.
      virtual auto ResetToDefaults() -> void;

      //! Resets only auto-exposure settings to their default values.
      virtual auto ResetAutoExposureDefaults() -> void;

      //! Resets the auto-exposure history for all views to the given EV.
      //!
      //! The EV value is referenced to ISO 100 (i.e. EV100).
      virtual auto ResetAutoExposure(float initial_ev) -> void;

      // Cache invalidation
      [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

    private:
      auto UpdateAutoExposureTarget() -> void;

      static constexpr auto kExposureModeKey = "post_process.exposure.mode";
      static constexpr auto kExposureEnabledKey
        = "post_process.exposure.enabled";
      static constexpr auto kExposureManualEVKey
        = "post_process.exposure.manual_ev";
      static constexpr auto kExposureCompensationKey
        = "post_process.exposure.compensation";
      static constexpr auto kExposureKeyKey = "post_process.exposure.key";

      static constexpr auto kTonemappingEnabledKey
        = "post_process.tonemapping.enabled";
      static constexpr auto kToneMapperKey = "post_process.tonemapping.mode";

      static constexpr auto kAutoExposureSpeedUpKey
        = "post_process.auto_exposure.speed_up";
      static constexpr auto kAutoExposureSpeedDownKey
        = "post_process.auto_exposure.speed_down";
      static constexpr auto kAutoExposureLowPercentileKey
        = "post_process.auto_exposure.low_percentile";
      static constexpr auto kAutoExposureHighPercentileKey
        = "post_process.auto_exposure.high_percentile";
      static constexpr auto kAutoExposureMinLogLumKey
        = "post_process.auto_exposure.min_log_lum";
      static constexpr auto kAutoExposureLogLumRangeKey
        = "post_process.auto_exposure.log_lum_range";
      static constexpr auto kAutoExposureTargetLumKey
        = "post_process.auto_exposure.target_lum";
      static constexpr auto kAutoExposureMeteringKey
        = "post_process.auto_exposure.metering";

      observer_ptr<RenderingPipeline> pipeline_;
      observer_ptr<CameraSettingsService> camera_settings_;
      observer_ptr<scene::Scene> scene_;
      mutable std::atomic_uint64_t epoch_ { 0 };
      mutable std::string last_camera_id_;
    };

  } // namespace ui
} // namespace examples
} // namespace oxygen
