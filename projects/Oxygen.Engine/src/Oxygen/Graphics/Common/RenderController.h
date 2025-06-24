//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "ShaderManager.h"

#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen {

class Graphics;

namespace graphics {

  class CommandList;
  class CommandQueue;
  class CommandRecorder;
  class DepthPrePass;
  class DescriptorAllocationStrategy;
  class DescriptorAllocator;
  class RenderPass;
  class ResourceRegistry;
  class Surface;
  struct DepthPrePassConfig;

  //! Orchestrates the frame render loop.
  /*!
   The frame render loop in this engine is managed by the `RenderController`
   class, which orchestrates all per-frame operations. Each frame begins with
   BeginFrame(), where the renderer checks for surface resizes, synchronizes
   with the GPU to ensure previous frame completion, and processes any
   deferred resource releases. After these preparations, the application’s
   rendering logic is executed, typically involving command recording and
   resource updates. The frame concludes with EndFrame(), which presents the
   rendered image to the display and advances the frame index for the next
   iteration.

   __Parallel Rendering and Work Submission__

   To maximize throughput and minimize CPU-GPU idle time, the engine supports
   multiple frames in flight. This is achieved by decoupling the application’s
   frame submission from the actual rendering work using a dedicated render
   thread (RenderThread). The application submits FrameRenderTask objects to
   the renderer, which queues them for execution. The render thread consumes
   these tasks, ensuring that the CPU can prepare new frames while the GPU is
   still processing previous ones. The number of frames in flight is
   configurable, providing a balance between latency and performance.

   __Coordination and Synchronization__

   Work coordination between the application and the renderer is handled
   through a producer-consumer model. The application acts as the producer,
   submitting frame tasks, while the render thread is the consumer, executing
   these tasks in order. The render thread enforces back-pressure by limiting
   the queue size to the number of frames in flight, ensuring the application
   cannot get too far ahead of the GPU. Synchronization primitives and
   per-frame tracking structures ensure that resources are only released when
   the GPU has finished using them, and that command lists are properly
   managed. This design guarantees safe, efficient, and parallel rendering,
   allowing for smooth frame delivery and optimal GPU utilization.

   __Overview of the Frame Render Loop__ \verbatim Application Renderer
   RenderThread             GPU
   |                          |                        |                    |
   |--Submit(Frame Render)--->|                        |                    |
   |                          |--enqueue task--------->|                    |
   |                          |                        |                    |
   |                          |        (waits)---------|                    |
   |                          |                        |                    |
   |                          |<-----BeginFrame()------|                    |
   |                          |---->Wait for previous  |                    |
   |                          |      frame GPU done    |                    |
   |                          |<-----GPU done-------------------------------|
   |                          |--Check resize/sync     |                    |
   |                          |--Deferred releases     |                    |
   |                          |--Release cmd lists     |                    |
   |                          |----------------------->|                    |
   |<----------------Execute Render Frame Task---------|                    |
   |--AcquireCommandRecorder->|                        |                    |
   |--Record commands-------->|                        |                    |
   |     ...                  |                        |                    |
   |-------------------------------------------------->|                    |
   |                          |<--------EndFrame()-----|                    |
   |                          |--Submit to Command Lists to GPU------------>|
   |                          |---->Present            |                    |
   |                          |--Advance frame index   |                    |
   |                          |----------------------->|                    |
   |                          |                        |                    |
   \endverbatim

   __Command Recording, Batching, and Submission__

   Command recording happens during the execution of the 'Render Frame Task',
   allowing a flexible model with immediate command list submission or
   batching of multiple command lists in one submission.

   When a CommandRecorder is disposed of, its command list is added by the
   RenderController to an ordered collection of pending command lists for the
   current frame. The 'Render Frame Task' can choose to submit command lists
   immediately or defer them for batch submission. At any point during the
   frame render cycle, the application or engine can call
   FlushPendingCommandLists() to submit all pending command lists for the
   current frame, enabling explicit control over command list submission and
   advanced batching strategies.

   The submission mode for command recorders can be specified during
   acquisition, allowing flexibility in choosing between immediate submission
   and deferred batching.

   EndFrame() always calls FlushPendingCommandLists(), ensuring that no
   command lists are left un-submitted and maintaining correct timeline
   synchronization and resource management.
  */
  class RenderController
    : public Composition,
      public std::enable_shared_from_this<RenderController> {
  public:
    OXYGEN_GFX_API RenderController(std::string_view name,
      std::weak_ptr<Graphics> gfx_weak, std::weak_ptr<Surface> surface_weak,
      uint32_t frames_in_flight = kFrameBufferCount - 1);

    OXYGEN_GFX_API ~RenderController() override;

    OXYGEN_MAKE_NON_COPYABLE(RenderController)
    OXYGEN_DEFAULT_MOVABLE(RenderController)

    // ReSharper disable once CppHiddenFunction - hidden in backend API
    OXYGEN_GFX_API auto GetGraphics() -> Graphics&;
    // ReSharper disable once CppHiddenFunction - hidden in backend API
    OXYGEN_GFX_API auto GetGraphics() const -> const Graphics&;

    OXYGEN_GFX_API auto GetDescriptorAllocator() const
      -> const DescriptorAllocator&;
    OXYGEN_GFX_API auto GetDescriptorAllocator() -> DescriptorAllocator&;

    OXYGEN_GFX_API auto GetResourceRegistry() const -> const ResourceRegistry&;
    OXYGEN_GFX_API auto GetResourceRegistry() -> ResourceRegistry&;

    OXYGEN_GFX_API auto Submit(FrameRenderTask task) -> void;
    OXYGEN_GFX_API auto Stop() -> void;

    //! Acquires a command recorder for recording rendering, compute, or
    //! copy commands.
    /*!
     Acquires and returns a unique pointer to a `CommandRecorder` for the
     specified queue and command list name. The returned recorder is
     **ready** for recording commands for the current frame.

     The returned pointer uses a custom deleter that ensures the command
     recorder is properly disposed of and its command list is submitted or
     batched according to the renderer submission strategy. If
     `immediate_submission` is true, the command list is submitted as soon
     as the recorder is destroyed; otherwise, it is batched for later
     submission.

     \param queue_name The name of the command queue to record commands to.
     \param command_list_name The name for the command list, used for
            debugging and tracking.
     \param immediate_submission If true, the command list is submitted
            immediately upon recorder destruction; if false, it is batched
            for later submission.
     \return A unique pointer to a `CommandRecorder` with a custom deleter
            for proper submission and cleanup.
    */
    [[nodiscard]] OXYGEN_GFX_API auto AcquireCommandRecorder(
      std::string_view queue_name, std::string_view command_list_name,
      bool immediate_submission = true) -> std::unique_ptr<CommandRecorder,
      std::function<void(CommandRecorder*)>>;

    [[nodiscard]] auto CurrentFrameIndex() const
    {
      return current_frame_index_;
    }

    [[nodiscard]] auto GetPerFrameResourceManager() const
      -> detail::PerFrameResourceManager&
    {
      return per_frame_resource_manager_;
    }

    OXYGEN_GFX_API virtual auto FlushPendingCommandLists() -> void;

    virtual auto CreateDepthPrePass(std::shared_ptr<DepthPrePassConfig> config)
      -> std::shared_ptr<RenderPass>
      = 0;

    // Returns a generic no-op render pass (NullRenderPass).
    OXYGEN_GFX_API auto CreateNullRenderPass() -> std::shared_ptr<RenderPass>;

  protected:
    [[nodiscard]] virtual auto CreateCommandRecorder(CommandList* command_list,
      CommandQueue* target_queue) -> std::unique_ptr<CommandRecorder>
      = 0;

    OXYGEN_GFX_API virtual auto BeginFrame() -> void;
    OXYGEN_GFX_API virtual auto EndFrame() -> void;

  private:
    auto HandleSurfaceResize(Surface& surface) -> void;

    std::weak_ptr<Graphics> gfx_weak_;
    std::weak_ptr<Surface> surface_weak_;

    //! Holds the data to manage the frame render cycle.
    struct Frame {
      //! Synchronization timeline values for all queues involved in this cycle.
      std::unordered_map<CommandQueue*, uint64_t> timeline_values;
      //! Command lists, submitted but still pending execution.
      std::vector<std::pair<std::shared_ptr<CommandList>, CommandQueue*>>
        pending_command_lists;
    };

    uint32_t frame_count_;
    std::unique_ptr<Frame[]> frames_;
    uint32_t current_frame_index_ { 0 };

    mutable detail::PerFrameResourceManager per_frame_resource_manager_;
  };

} // namespace graphics
} // namespace oxygen
