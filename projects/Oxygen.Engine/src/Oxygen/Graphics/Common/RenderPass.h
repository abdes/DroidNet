//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::graphics {

class CommandRecorder;
struct Color;
struct Scissors;
struct ViewPort;

//! Abstract base class for a modular, coroutine-based render pass.
/*!
 RenderPass encapsulates a single stage of the rendering pipeline, such as
 geometry, shading, or post-processing. It is designed for use with modern,
 explicit graphics APIs (D3D12, Vulkan) and supports asynchronous (coroutine)
 resource preparation and execution, enabling fine-grained scheduling,
 parallelism, and non-blocking GPU work.

 Key design points:
  - Passes are modular and composable, supporting Forward+, deferred, or custom
    pipelines.
  - Resource state transitions and barriers are explicit and handled in
    PrepareResources.
  - Execution is coroutine-based, allowing for async GPU work, resource uploads,
    and synchronization.
  - Viewport, scissors, and clear color are set independently for flexibility
    and API consistency.
  - Passes can be enabled/disabled at runtime for debugging or feature toggling.

 Best practices for Forward+ and modern rendering:
  - Use PrepareResources to declare and transition all resources needed by the
    pass (framebuffers, buffers, etc.).
  - Use Execute for the main rendering logic, including pipeline setup, resource
    binding, and draw/dispatch calls.
  - Keep passes focused and modular (e.g., geometry pass, light culling pass,
    shading pass).
  - Use coroutines to compose passes, enable async GPU waits, and maximize
    parallelism.
  - Explicitly manage resource states to avoid hazards and maximize performance.
*/
class RenderPass : public oxygen::Composition, public oxygen::Named {
public:
    explicit RenderPass(const std::string_view name);
    virtual ~RenderPass() = default;

    OXYGEN_DEFAULT_COPYABLE(RenderPass)
    OXYGEN_DEFAULT_MOVABLE(RenderPass)

    //! Prepare and transition all resources needed for this pass.
    /*!
     This coroutine should explicitly declare and transition all input/output
     resources (textures, buffers, framebuffers, etc.) to the correct states for
     this pass, using the provided CommandRecorder. This includes inserting
     resource barriers, preparing descriptor tables, and ensuring all
     dependencies are met before execution.

     In Forward+ and modern explicit APIs, this step is critical for correctness
     and performance.

     \param recorder The command recorder for issuing resource transitions and
            setup commands.

     \return Coroutine handle (co::Co<>)
    */
    virtual co::Co<> PrepareResources(CommandRecorder& recorder) = 0;

    //! Execute the main rendering logic for this pass.
    /*!
     This coroutine should perform all rendering commands for the pass,
     including pipeline setup, resource binding, draw/dispatch calls, and any
     per-pass logic. It is called after PrepareResources and assumes all
     resources are in the correct state.

     Use this method to implement the core of geometry, shading, or
     post-processing passes.

     \param recorder The command recorder for issuing rendering commands.

     \return Coroutine handle (co::Co<>)
    */
    virtual co::Co<> Execute(CommandRecorder& recorder) = 0;

    //! Set the viewport for this pass.
    virtual void SetViewport(const ViewPort& viewport) = 0;

    //! Set the scissors rectangle for this pass.
    virtual void SetScissors(const Scissors& scissors) = 0;

    //! Set the clear color for this pass's framebuffer.
    virtual void SetClearColor(const Color& color) = 0;

    //! Enable or disable this pass at runtime.
    virtual void SetEnabled(bool enabled) = 0;

    //! Query whether this pass is enabled.
    virtual auto IsEnabled() const -> bool = 0;

    //! Get the name of this pass (from Named interface).
    [[nodiscard]] virtual auto GetName() const noexcept -> std::string_view override;

    //! Set the name of this pass (from Named interface).
    virtual void SetName(std::string_view name) noexcept override;
};

} // namespace oxygen::graphics
