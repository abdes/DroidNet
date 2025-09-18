//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen {
class Graphics;
}

namespace oxygen::graphics {
class Framebuffer;
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::scene {
class Light;
} // namespace oxygen::scene

namespace oxygen::engine {

class Renderer;
class RenderPass;

//=== Pass Type List and Compile-Time Indexing ===----------------------------//

template <typename... Ts> struct PassTypeList {
  static constexpr std::size_t size = sizeof...(Ts);
};

template <typename T, typename List> struct PassIndexOf;

// Base case: searching in an empty list is ill-formed; provide a sentinel to
// improve diagnostic clarity instead of incomplete type errors.

template <typename T, typename... Ts>
struct PassIndexOf<T, PassTypeList<T, Ts...>>
  : std::integral_constant<std::size_t, 0> { };

template <typename T, typename U, typename... Ts>
struct PassIndexOf<T, PassTypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + PassIndexOf<T, PassTypeList<Ts...>>::value> { };

// Provide explicit specialization when the searched-for type does not exist to
// break recursion with clearer error message.
template <typename T> struct PassIndexOf<T, PassTypeList<>> {
  static_assert(sizeof(T) == 0, "Pass type not found in KnownPassTypes");
};

// Forward declare the know pass classes here.
class DepthPrePass;
class ShaderPass;
class TransparentPass; // newly added transparent geometry pass

/*!
 Defines the list of all known render pass types for the current render graph.
 The order of types determines their index. Only append new types to maintain
 binary compatibility. Update this list as new passes are added.
*/
using KnownPassTypes = PassTypeList<DepthPrePass, ShaderPass, TransparentPass>;

//! The number of known pass types, used for static array sizing and sanity
//! checks.
static constexpr std::size_t kNumPassTypes = KnownPassTypes::size;

//=== Render Context Definition ===-------------------------------------------//

//! Holds all data shared across the render graph for a single frame.
/*!
 Contains engine-wide and application-wide data that is shared across passes.
 Backend resources and per-pass configuration are owned/configured by each pass,
 not by the context.

 @see Renderer, RenderPass
*/
struct RenderContext {
  // Application data
  // Camera parameters
  // (view/projection matrices, camera position, frustum, etc.)
  // Add your camera struct or fields here as needed

  // Scene constants (lighting environment, fog, global exposure, etc.)
  // Add your scene constant struct or fields here as needed

  // Legacy AoS RenderItem lists removed. Passes now consume PreparedSceneFrame
  // (prepared_frame) for per-draw data.

  // TODO: Light lists
  // std::vector<scene::Light> light_list;

  // Pass enable/disable flags (by pass index in the KnownKnownPassTypes)
  std::unordered_map<size_t, bool> pass_enable_flags;

  //! Framebuffer object for broader rendering context.
  std::shared_ptr<const graphics::Framebuffer> framebuffer = nullptr;

  //! The constant buffer containing scene-wide constants.
  /*!
   This buffer should be prepared and updated by the caller before the render
   graph executes. It is bound directly as a root CBV (using its GPU virtual
   address). Render passes will need to ensure that the root signature is set
   consistently with the shader's expectations.

   @note This field is mandatory.
  */
  std::shared_ptr<const graphics::Buffer> scene_constants;

  //! The constant buffer containing material constants for the current render
  //! item.
  /*!
   This buffer should be prepared and updated by the caller before the render
   graph executes. It is bound directly as a root CBV (using its GPU virtual
   address). Render passes will need to ensure that the root signature is set
   consistently with the shader's expectations.

   @note This field is optional and may be nullptr if no material data is
   needed.
  */
  std::shared_ptr<const graphics::Buffer> material_constants;

  //=== SoA Prepared Frame (Renderer Finalization Output) ===-----------------
  // Non-owning pointer to the renderer's immutable per-frame finalized SoA
  // snapshot. Set by Renderer::PreExecute after
  // BuildFrame/FinalizeScenePrepSoA. Nullptr until the renderer wires it;
  // passes must gracefully handle null (legacy path) during the migration
  // phase. Will become mandatory once AoS is fully removed.
  observer_ptr<const struct PreparedSceneFrame> prepared_frame { nullptr };

  //=== Renderer / Graphics ===-----------------------------------------------//

  //! The renderer executing the render graph. Guaranteed to be non-null during
  //! the render graph execution.
  auto GetRenderer() const -> auto& { return *renderer_; }

  //! The graphics system managing the frame rendering process. Guaranteed to
  //! be non-null during the render graph execution.
  auto GetGraphics() const -> auto& { return *graphics_; }

  //=== Pass Management ===---------------------------------------------------//

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
    return static_cast<PassT*>(known_passes_[idx].get());
  }

  /*!
   Registers a pass pointer for type PassT in the pass pointer registry.

   Typically called by the render graph code, responsible for setting up and
   executing the pass, after it completes. Registering the pass makes it
   available for cross-pass access.

   @tparam PassT The pass type to register. Must be listed in KnownPassTypes.
   @param pass Pointer to the pass instance to register.

   @note Compile-time error if PassT is not in KnownPassTypes.
   @see GetPass
  */
  template <typename PassT> auto RegisterPass(PassT* pass) const -> void
  {
    constexpr std::size_t idx = PassIndexOf<PassT, KnownPassTypes>::value;
    static_assert(idx < kNumPassTypes, "Pass type not in KnownPassTypes");
    known_passes_[idx].reset(pass);
  }

private:
  friend class Renderer;

  //! Sets the renderer and graphics for the current render graph run.
  auto SetRenderer(Renderer* the_renderer, oxygen::Graphics* the_graphics) const
  {
    renderer_.reset(the_renderer);
    graphics_.reset(the_graphics);
  }

  //! Resets the render context for a new graph run.
  /*!
   Called at the start (or end) of each graph run only by the Renderer.
   Performs a shallow, per-frame cleanup of engine-managed pointers so that
   subsequent frames begin with a clean slate while preserving any
   application-populated value semantics (the application is expected to
   repopulate them each frame as needed):
   - Clears the pass pointer registry.
   - Resets the renderer and graphics pointers to null.
  - Clears scene_constants and material_constants (renderer-owned snapshots).
   - Does NOT touch persistent configuration fields the application may add
     in the future (only engine-injected per-frame pointers are cleared).
  */
  auto Reset() -> void
  {
    for (auto& p : known_passes_) {
      p.reset(nullptr);
    }
    renderer_.reset(nullptr);
    graphics_.reset(nullptr);
    scene_constants.reset();
    material_constants.reset();
    prepared_frame.reset();
  }
  mutable observer_ptr<Renderer> renderer_ { nullptr };
  mutable observer_ptr<oxygen::Graphics> graphics_ { nullptr };
  mutable std::array<observer_ptr<RenderPass>, kNumPassTypes> known_passes_ {
    { nullptr }
  };
};

} // namespace oxygen::engine
