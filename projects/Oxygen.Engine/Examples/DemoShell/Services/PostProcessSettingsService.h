//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen {
namespace engine {
  enum class ExposureMode : std::uint32_t;
  enum class ToneMapper : std::uint32_t;
} // namespace engine

namespace examples {

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

      // Compositing
      [[nodiscard]] virtual auto GetCompositingEnabled() const -> bool;
      virtual auto SetCompositingEnabled(bool enabled) -> void;

      [[nodiscard]] virtual auto GetCompositingAlpha() const -> float;
      virtual auto SetCompositingAlpha(float alpha) -> void;

      // Exposure
      [[nodiscard]] virtual auto GetExposureMode() const
        -> engine::ExposureMode;
      virtual auto SetExposureMode(engine::ExposureMode mode) -> void;

      [[nodiscard]] virtual auto GetManualExposureEV100() const -> float;
      virtual auto SetManualExposureEV100(float ev100) -> void;

      [[nodiscard]] virtual auto GetExposureCompensation() const -> float;
      virtual auto SetExposureCompensation(float stops) -> void;

      // Tonemapping
      [[nodiscard]] virtual auto GetTonemappingEnabled() const -> bool;
      virtual auto SetTonemappingEnabled(bool enabled) -> void;

      [[nodiscard]] virtual auto GetToneMapper() const -> engine::ToneMapper;
      virtual auto SetToneMapper(engine::ToneMapper mode) -> void;

      // Cache invalidation
      [[nodiscard]] virtual auto GetEpoch() const noexcept -> std::uint64_t;

    protected:
      [[nodiscard]] virtual auto ResolveSettings() const noexcept
        -> observer_ptr<SettingsService>;

    private:
      static constexpr auto kCompositingEnabledKey
        = "post_process.compositing.enabled";
      static constexpr auto kCompositingAlphaKey
        = "post_process.compositing.alpha";

      static constexpr auto kExposureModeKey = "post_process.exposure.mode";
      static constexpr auto kExposureManualEV100Key
        = "post_process.exposure.manual_ev100";
      static constexpr auto kExposureCompensationKey
        = "post_process.exposure.compensation";

      static constexpr auto kTonemappingEnabledKey
        = "post_process.tonemapping.enabled";
      static constexpr auto kToneMapperKey = "post_process.tonemapping.mode";

      observer_ptr<RenderingPipeline> pipeline_ {};
      mutable std::atomic_uint64_t epoch_ { 0 };
    };

  } // namespace ui
} // namespace examples
} // namespace oxygen
