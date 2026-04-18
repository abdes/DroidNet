//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Time/SimulationClock.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class Buffer;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::vortex {

struct CompositionView;
class Renderer;
class RenderPass;

template <typename... Ts> struct PassTypeList {
  static constexpr std::size_t size = sizeof...(Ts);
};

template <typename T, typename List> struct PassIndexOf;

template <typename T, typename... Ts>
struct PassIndexOf<T, PassTypeList<T, Ts...>>
  : std::integral_constant<std::size_t, 0> { };

template <typename T, typename U, typename... Ts>
struct PassIndexOf<T, PassTypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + PassIndexOf<T, PassTypeList<Ts...>>::value> { };

template <typename T> struct PassIndexOf<T, PassTypeList<>> {
  static_assert(sizeof(T) == 0, "Pass type not found in KnownPassTypes");
};

using KnownPassTypes = PassTypeList<>;
static constexpr std::size_t kNumPassTypes = KnownPassTypes::size;

struct RenderContext {
  struct ViewExecutionEntry {
    oxygen::ViewId view_id {};
    bool is_scene_view { false };
    observer_ptr<const CompositionView> composition_view;
    std::optional<ShadingMode> shading_mode_override;
    observer_ptr<const oxygen::ResolvedView> resolved_view;
    observer_ptr<graphics::Framebuffer> render_target;
    observer_ptr<graphics::Framebuffer> composite_source;
    observer_ptr<graphics::Framebuffer> primary_target;
  };

  std::unordered_map<size_t, bool> pass_enable_flags;
  observer_ptr<const graphics::Framebuffer> pass_target;
  std::shared_ptr<const graphics::Buffer> view_constants;
  std::shared_ptr<const graphics::Buffer> material_constants;
  ShaderDebugMode shader_debug_mode { ShaderDebugMode::kDisabled };

  struct ViewSpecific {
    oxygen::ViewId view_id {};
    oxygen::ViewId exposure_view_id {};
    observer_ptr<const CompositionView> composition_view;
    std::optional<ShadingMode> shading_mode_override;
    observer_ptr<const oxygen::ResolvedView> resolved_view;
    observer_ptr<const struct PreparedSceneFrame> prepared_frame;
    mutable DepthPrePassMode depth_prepass_mode {
      DepthPrePassMode::kOpaqueAndMasked
    };
    mutable DepthPrePassCompleteness depth_prepass_completeness {
      DepthPrePassCompleteness::kIncomplete
    };

    [[nodiscard]] auto HasPlannedDepthPrePass() const noexcept -> bool
    {
      return depth_prepass_mode != DepthPrePassMode::kDisabled;
    }

    [[nodiscard]] auto IsEarlyDepthComplete() const noexcept -> bool
    {
      return depth_prepass_completeness == DepthPrePassCompleteness::kComplete;
    }
  };

  ViewSpecific current_view {};
  std::vector<ViewExecutionEntry> frame_views {};
  std::size_t active_view_index { std::numeric_limits<std::size_t>::max() };
  frame::Slot frame_slot { frame::kInvalidSlot };
  frame::SequenceNumber frame_sequence { 0 };
  std::unordered_map<oxygen::ViewId, observer_ptr<graphics::Framebuffer>>
    view_outputs;
  float delta_time { time::SimulationClock::kMinDeltaTimeSeconds };
  observer_ptr<oxygen::scene::Scene> scene { nullptr };

  [[nodiscard]] auto GetSceneMutable() noexcept
    -> observer_ptr<oxygen::scene::Scene>
  {
    return scene;
  }

  [[nodiscard]] auto GetSceneMutable() const noexcept
    -> observer_ptr<oxygen::scene::Scene>
  {
    return scene;
  }

  [[nodiscard]] auto GetScene() const noexcept
    -> observer_ptr<const oxygen::scene::Scene>
  {
    return scene;
  }

  [[nodiscard]] auto GetCurrentCompositionView() const noexcept
    -> const CompositionView*
  {
    return current_view.composition_view.get();
  }

  [[nodiscard]] auto GetActiveViewEntry() const noexcept
    -> const ViewExecutionEntry*
  {
    return active_view_index < frame_views.size()
      ? &frame_views[active_view_index]
      : nullptr;
  }

  auto GetRenderer() const -> auto& { return *renderer_; }
  auto GetGraphics() const -> auto& { return *graphics_; }

  template <typename PassT> auto GetPass() const -> PassT*
  {
    constexpr std::size_t idx = PassIndexOf<PassT, KnownPassTypes>::value;
    static_assert(idx < kNumPassTypes, "Pass type not in KnownPassTypes");
    return static_cast<PassT*>(known_passes_[idx].get());
  }

  template <typename PassT> auto RegisterPass(PassT* pass) const -> void
  {
    constexpr std::size_t idx = PassIndexOf<PassT, KnownPassTypes>::value;
    static_assert(idx < kNumPassTypes, "Pass type not in KnownPassTypes");
    known_passes_[idx].reset(pass);
  }

  auto ClearRegisteredPasses() const -> void
  {
    for (auto& pass : known_passes_) {
      pass.reset(nullptr);
    }
  }

  auto Reset() -> void
  {
    ClearRegisteredPasses();
    renderer_.reset(nullptr);
    graphics_.reset(nullptr);
    view_constants.reset();
    material_constants.reset();
    shader_debug_mode = ShaderDebugMode::kDisabled;
    pass_target.reset(nullptr);
    current_view = ViewSpecific {};
    frame_views.clear();
    active_view_index = std::numeric_limits<std::size_t>::max();
    view_outputs.clear();
    scene.reset(nullptr);
    frame_slot = frame::kInvalidSlot;
    frame_sequence = frame::SequenceNumber {};
    delta_time = time::SimulationClock::kMinDeltaTimeSeconds;
    pass_enable_flags.clear();
  }

private:
  friend class Renderer;

  auto SetRenderer(Renderer* renderer, oxygen::Graphics* graphics) const -> void
  {
    renderer_.reset(renderer);
    graphics_.reset(graphics);
  }

  mutable observer_ptr<Renderer> renderer_;
  mutable observer_ptr<oxygen::Graphics> graphics_;
  mutable std::array<observer_ptr<RenderPass>, kNumPassTypes> known_passes_ {};
};

} // namespace oxygen::vortex
