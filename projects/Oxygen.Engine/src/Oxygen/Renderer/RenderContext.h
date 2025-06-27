//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace oxygen::graphics {
class Framebuffer;
class Buffer;
class CommandRecorder;
class RenderController;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Light;
} // namespace oxygen::scene

namespace oxygen::engine {

class Renderer;
struct RenderItem;
class RenderPass;

//=== Pass Type List and Compile-Time Indexing ===-------------------------//

/*!
 Defines the list of all known render pass types for the current render graph.
 The order of types determines their index. Only append new types to maintain
 binary compatibility. Update this list as new passes are added.
*/
template <typename... Ts> struct PassTypeList {
  static constexpr std::size_t size = sizeof...(Ts);
};

// Example: forward declare your pass types here
class DepthPrePass;
// class LightingPass;
// class PostProcessPass;
// ... add more as needed ...

using KnownPassTypes = PassTypeList<DepthPrePass
  //, LightingPass
  //, PostProcessPass
  // ...
  >;

static constexpr std::size_t kNumPassTypes = KnownPassTypes::size;

/*!\
 Compile-time metafunction to get the index of a pass type in KnownPassTypes.
*/
template <typename T, typename List> struct PassIndexOf;

template <typename T, typename... Ts>
struct PassIndexOf<T, PassTypeList<T, Ts...>>
  : std::integral_constant<std::size_t, 0> { };

template <typename T, typename U, typename... Ts>
struct PassIndexOf<T, PassTypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + PassIndexOf<T, PassTypeList<Ts...>>::value> { };

//=== Render Context Definition ===-------------------------------------------//

//! Holds all data shared across the render graph for a single frame.
/*!
    Contains engine-wide and application-wide data that is shared across passes.
    Backend resources and per-pass configuration are owned/configured by each
   pass, not by the context.

    @see Renderer, RenderPass
*/
struct RenderContext {
  // Engine data
  uint64_t frame_index = 0;
  float frame_time = 0.0f;
  uint32_t random_seed = 0;
  // ... profiling/timing context fields ...

  // Application data
  // Camera parameters
  // (view/projection matrices, camera position, frustum, etc.)
  // Add your camera struct or fields here as needed

  // Scene constants (lighting environment, fog, global exposure, etc.)
  // Add your scene constant struct or fields here as needed

  // Render item lists
  std::vector<const RenderItem*> opaque_draw_list;
  std::vector<const RenderItem*> transparent_draw_list;
  // ... add more lists for decals, particles, etc. as needed ...

  // TODO: Light lists
  // std::vector<scene::Light> light_list;

  // Pass enable/disable flags (by pass name or type)
  std::unordered_map<std::string, bool> pass_enable_flags;

  //! The current rendererexecuting the render graph. Guaranteed to be non-null
  //! during the render graph execution.
  Renderer* renderer;

  //! The render controller managing the frame rendering process. Guaranteed to
  //! be non-null during the render graph execution.
  graphics::RenderController* render_controller;

  //! Framebuffer object for broader rendering context.
  std::shared_ptr<const graphics::Framebuffer> framebuffer = nullptr;

  //! The constant buffer containing scene-wide constants.
  /*!
   This buffer  should be prepared and updated by the caller before the render
   graph executes. It is bound directly as a root CBV (using its GPU virtual
   address) and does not require a descriptor in the descriptor heap. This field
   is mandatory.
  */
  std::shared_ptr<const graphics::Buffer> scene_constants;

  //=== Pass Management ===---------------------------------------------------//

  //! Pass pointer registry for pass-to-pass data flow (O(1) array,
  //! type-indexed)
  std::array<RenderPass*, kNumPassTypes> pass_ptrs { { nullptr } };

  /*!
   Returns a pointer to the registered pass of type PassT, or nullptr if not
   registered.

   Typically called by a render pass or graph logic to access another pass's
   interface or data during graph execution. This is the way to explicitly
   manage dependencies between passes. A pass that needs input from a previously
   executed pass, calls this method to retrieve the pointer to that pass's
   interface. It is up to the caller to decide what to do when the pass pointer
   is null (not executed, executed but not registered, etc.).

   @tparam PassT The pass type to retrieve. Must be listed in KnownPassTypes.
   @return Pointer to the registered pass, or nullptr if not registered.

   @note Compile-time error if PassT is not in KnownPassTypes.
   @see RegisterPass
  */
  template <typename PassT> auto GetPass() const -> PassT*
  {
    constexpr std::size_t idx = PassIndexOf<PassT, KnownPassTypes>::value;
    static_assert(idx < kNumPassTypes, "Pass type not in KnownPassTypes");
    return static_cast<PassT*>(pass_ptrs[idx]);
  }

  /*!
   Registers a pass pointer for type PassT in the pass pointer registry.

   Typically called by the the render graph code, responsible for setting up and
   executing the pass, after it completes. Registering the pass makes it
   available for cross-pass access.

   @tparam PassT The pass type to register. Must be listed in KnownPassTypes.
   @param pass Pointer to the pass instance to register.

   @note Compile-time error if PassT is not in KnownPassTypes.
   @see GetPass
  */
  template <typename PassT> auto RegisterPass(PassT* pass) -> void
  {
    constexpr std::size_t idx = PassIndexOf<PassT, KnownPassTypes>::value;
    static_assert(idx < kNumPassTypes, "Pass type not in KnownPassTypes");
    pass_ptrs[idx] = pass;
  }
};

} // namespace oxygen::engine
