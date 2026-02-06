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

      [[nodiscard]] virtual auto GetManualExposureEV100() const -> float;
      virtual auto SetManualExposureEV100(float ev100) -> void;

      [[nodiscard]] virtual auto GetManualCameraAperture() const -> float;
      virtual auto SetManualCameraAperture(float aperture) -> void;

      [[nodiscard]] virtual auto GetManualCameraShutterRate() const -> float;
      virtual auto SetManualCameraShutterRate(float shutter_rate) -> void;

      [[nodiscard]] virtual auto GetManualCameraIso() const -> float;
      virtual auto SetManualCameraIso(float iso) -> void;

      [[nodiscard]] virtual auto GetManualCameraEV100() const -> float;

      [[nodiscard]] virtual auto GetExposureCompensation() const -> float;
      virtual auto SetExposureCompensation(float stops) -> void;

      [[nodiscard]] virtual auto GetExposureKey() const -> float;
      virtual auto SetExposureKey(float exposure_key) -> void;

      // Tonemapping
      [[nodiscard]] virtual auto GetTonemappingEnabled() const -> bool;
      virtual auto SetTonemappingEnabled(bool enabled) -> void;

      [[nodiscard]] virtual auto GetToneMapper() const -> engine::ToneMapper;
      virtual auto SetToneMapper(engine::ToneMapper mode) -> void;

      // Cache invalidation
      [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

    private:
      static constexpr auto kExposureModeKey = "post_process.exposure.mode";
      static constexpr auto kExposureEnabledKey
        = "post_process.exposure.enabled";
      static constexpr auto kExposureManualEV100Key
        = "post_process.exposure.manual_ev100";
      static constexpr auto kExposureCompensationKey
        = "post_process.exposure.compensation";
      static constexpr auto kExposureKeyKey = "post_process.exposure.key";

      static constexpr auto kTonemappingEnabledKey
        = "post_process.tonemapping.enabled";
      static constexpr auto kToneMapperKey = "post_process.tonemapping.mode";

      observer_ptr<RenderingPipeline> pipeline_ {};
      observer_ptr<CameraSettingsService> camera_settings_ {};
      observer_ptr<scene::Scene> scene_ {};
      mutable std::atomic_uint64_t epoch_ { 0 };
      mutable std::string last_camera_id_ {};
    };

  } // namespace ui
} // namespace examples
} // namespace oxygen
