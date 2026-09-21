//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "DemoShell/Runtime/SceneActivationPolicy.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Scene/Camera/CameraExposure.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Vortex/Types/ExposureSettingsStatus.h>

namespace oxygen {
namespace vortex {
  class Renderer;
}
namespace scene {
  class Scene;
}
} // namespace oxygen

namespace oxygen::examples {

class CameraSettingsService;
class SettingsService;

namespace ui {

  //! Named endpoints for atomic exposure range edits; units follow the setter.
  struct ExposureRange {
    float minimum;
    float maximum;
  };

  //! Settings persistence for the PostProcessPanel.
  class PostProcessSettingsService {
  public:
    PostProcessSettingsService() = default;
    virtual ~PostProcessSettingsService() = default;

    OXYGEN_MAKE_NON_COPYABLE(PostProcessSettingsService)
    OXYGEN_MAKE_NON_MOVABLE(PostProcessSettingsService)

    auto SetSceneActivationPolicy(SceneActivationPolicy policy) -> void;
    [[nodiscard]] auto GetSceneActivationPolicy() const noexcept
      -> SceneActivationPolicy;
    auto BindMainView(ViewId view_id) -> void;
    auto OnFrameStart() -> void;
    [[nodiscard]] auto GetSceneRevision() const noexcept -> std::uint64_t;
    [[nodiscard]] auto GetExposureStatus() const
      -> std::optional<vortex::ExposureSettingsStatus>;
    [[nodiscard]] auto GetExposureSettings() const -> scene::ExposureSettings;
    //! Validate and apply one complete authored revision; invalid edits leave
    //! settings unchanged. The mask must be the scene-authored resource or
    //! zero.
    auto TrySetExposureSettings(const scene::ExposureSettings& settings)
      -> bool;
    auto SetExposureCompensationCurve(
      std::span<const scene::ExposureCompensationKey> keys) -> bool;
    [[nodiscard]] auto GetValidationError() const noexcept -> std::string_view;
    [[nodiscard]] auto HasActiveCamera() const -> bool;
    [[nodiscard]] auto HasSceneMeteringMask() const -> bool;
    [[nodiscard]] auto GetUseSceneMeteringMask() const -> bool;
    auto SetUseSceneMeteringMask(bool enabled) -> void;
    [[nodiscard]] auto GetAutoExposureBlackInfluence() const -> float;
    auto SetAutoExposureBlackInfluence(float influence) -> void;
    [[nodiscard]] auto GetAutoExposureTransitionDistance() const -> float;
    auto SetAutoExposureTransitionDistance(float distance) -> void;
    auto SetAutoExposureRange(ExposureRange ev) -> void;
    auto SetAutoExposurePercentiles(ExposureRange percentiles) -> void;
    auto SetAutoExposureHistogramWindow(ExposureRange log_luminance) -> void;

    //! Binds the camera settings service used for camera exposure settings.
    virtual auto BindCameraSettings(
      observer_ptr<CameraSettingsService> camera_settings) -> void;

    //! Binds the active scene for post-process system updates.
    virtual auto BindScene(observer_ptr<scene::Scene> scene) -> void;
    virtual auto BindVortexRenderer(observer_ptr<vortex::Renderer> renderer)
      -> void;

    //! Applies the exposure fields owned by an environment preset.
    //!
    //! A transient preset changes runtime values without writing settings.
    //! Later ordinary edits persist only the fields explicitly changed.
    auto ApplyExposurePreset(engine::ExposureMode mode, float manual_ev,
      bool enabled, bool persist = true) -> void;

    // Exposure
    [[nodiscard]] virtual auto GetExposureEnabled() const -> bool;
    virtual auto SetExposureEnabled(bool enabled) -> void;

    [[nodiscard]] virtual auto GetExposureMode() const -> engine::ExposureMode;
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

    [[nodiscard]] virtual auto GetGamma() const -> float;
    virtual auto SetGamma(float gamma) -> void;

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

    [[nodiscard]] virtual auto GetAutoExposureMinEv() const -> float;
    virtual auto SetAutoExposureMinEv(float min_ev) -> void;

    [[nodiscard]] virtual auto GetAutoExposureMaxEv() const -> float;
    virtual auto SetAutoExposureMaxEv(float max_ev) -> void;

    [[nodiscard]] virtual auto GetAutoExposureMinLogLuminance() const -> float;
    virtual auto SetAutoExposureMinLogLuminance(float luminance) -> void;

    [[nodiscard]] virtual auto GetAutoExposureLogLuminanceRange() const
      -> float;
    virtual auto SetAutoExposureLogLuminanceRange(float range) -> void;

    [[nodiscard]] virtual auto GetAutoExposureTargetLuminance() const -> float;
    virtual auto SetAutoExposureTargetLuminance(float luminance) -> void;
    [[nodiscard]] virtual auto GetAutoExposureSpotMeterRadius() const -> float;
    virtual auto SetAutoExposureSpotMeterRadius(float radius) -> void;

    [[nodiscard]] virtual auto GetAutoExposureMeteringMode() const
      -> engine::MeteringMode;
    virtual auto SetAutoExposureMeteringMode(engine::MeteringMode mode) -> void;

    //! Resets all post-process settings to their default values.
    virtual auto ResetToDefaults() -> void;

    //! Resets only auto-exposure settings to their default values.
    virtual auto ResetAutoExposureDefaults() -> void;

    //! Submits an EV seed to currently registered exposure owners. Borrowing
    //! consumers follow their source; the renderer validates each view's mode.
    //!
    //! The EV value is referenced to ISO 100 (i.e. EV100).
    virtual auto ResetAutoExposure(float initial_ev) -> void;

    // Cache invalidation
    [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

  private:
    struct State {
      scene::ExposureSettings exposure;
      bool tonemapping_enabled { true };
      engine::ToneMapper tone_mapper { engine::ToneMapper::kAcesFitted };
      float gamma { 2.2F }; // NOLINT(*-magic-numbers)
    };
    struct ExposureFloatBinding {
      std::string_view key;
      float scene::ExposureSettings::* member;
      bool automatic;
    };
    static auto Defaults() -> State;
    static auto FloatBindings() -> std::span<const ExposureFloatBinding>;
    auto CaptureSceneDefaults() const -> State;
    auto EnsureStateLoaded() const -> void;
    auto ValidateExposure(const scene::ExposureSettings& requested) const
      -> bool;
    auto CommitExposure(const scene::ExposureSettings& requested) -> bool;
    auto SetExposureFloat(float scene::ExposureSettings::* member, float value,
      std::string_view key) -> void;
    auto PersistExposure(std::string_view key) const -> void;
    auto PersistAllExposure() const -> void;
    auto SetCameraExposureFloat(
      float scene::CameraExposure::* member, float value) -> void;
    auto SyncScenePostProcessState() -> void;

    static constexpr auto kExposureModeKey = "post_process.exposure.mode";
    static constexpr auto kExposureEnabledKey = "post_process.exposure.enabled";
    static constexpr auto kExposureManualEVKey
      = "post_process.exposure.manual_ev";
    static constexpr auto kExposureCompensationKey
      = "post_process.exposure.compensation";
    static constexpr auto kExposureKeyKey = "post_process.exposure.key";

    static constexpr auto kTonemappingEnabledKey
      = "post_process.tonemapping.enabled";
    static constexpr auto kToneMapperKey = "post_process.tonemapping.mode";
    static constexpr auto kGammaKey = "post_process.tonemapping.gamma";

    static constexpr auto kAutoExposureSpeedUpKey
      = "post_process.auto_exposure.speed_up";
    static constexpr auto kAutoExposureSpeedDownKey
      = "post_process.auto_exposure.speed_down";
    static constexpr auto kAutoExposureLowPercentileKey
      = "post_process.auto_exposure.low_percentile";
    static constexpr auto kAutoExposureHighPercentileKey
      = "post_process.auto_exposure.high_percentile";
    static constexpr auto kAutoExposureMinEvKey
      = "post_process.auto_exposure.min_ev";
    static constexpr auto kAutoExposureMaxEvKey
      = "post_process.auto_exposure.max_ev";
    static constexpr auto kAutoExposureMinLogLumKey
      = "post_process.auto_exposure.min_log_lum";
    static constexpr auto kAutoExposureLogLumRangeKey
      = "post_process.auto_exposure.log_lum_range";
    static constexpr auto kAutoExposureTargetLumKey
      = "post_process.auto_exposure.target_lum";
    static constexpr auto kAutoExposureSpotRadiusKey
      = "post_process.auto_exposure.spot_radius";
    static constexpr auto kAutoExposureMeteringKey
      = "post_process.auto_exposure.metering";

    static constexpr auto kBlackInfluenceKey
      = "post_process.auto_exposure.black_influence";
    static constexpr auto kTransitionDistanceKey
      = "post_process.auto_exposure.transition_distance_ev";
    static constexpr auto kCurveKey
      = "post_process.auto_exposure.compensation_curve";
    static constexpr auto kUseSceneMaskKey
      = "post_process.auto_exposure.use_scene_mask";

    SceneActivationPolicy activation_policy_ {
      SceneActivationPolicy::kRestorePreferences
    };
    mutable State state_;
    mutable bool state_initialized_ { false };
    mutable State scene_defaults_;
    mutable bool use_scene_mask_ { true };
    mutable std::string validation_error_;
    std::optional<ViewId> main_view_id_;
    std::uint64_t scene_revision_ { 0U };
    observer_ptr<CameraSettingsService> camera_settings_;
    observer_ptr<scene::Scene> scene_;
    observer_ptr<vortex::Renderer> vortex_renderer_;
    std::optional<engine::ExposureMode> transient_exposure_mode_;
    std::optional<float> transient_manual_exposure_ev_;
    std::optional<bool> transient_exposure_enabled_;
    mutable std::atomic_uint64_t epoch_ { 0 };
    std::optional<scene::CameraExposure> observed_camera_exposure_;
  };

} // namespace ui
} // namespace oxygen::examples
