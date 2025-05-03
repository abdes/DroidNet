//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Types/EngineResources.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

namespace imgui {
    class ImguiModule;
} // namespace imgui

namespace graphics {

    class Surface;

    class Renderer : public Composition, public std::enable_shared_from_this<Renderer> {
        OXYGEN_TYPED(Renderer)
    public:
        Renderer(
            std::weak_ptr<oxygen::Graphics> gfx_weak,
            std::weak_ptr<graphics::Surface> surface_weak,
            uint32_t frames_in_flight = oxygen::kFrameBufferCount - 1)
            : Renderer("Renderer", std::move(gfx_weak), std::move(surface_weak), frames_in_flight)
        {
        }

        //! Default constructor, sets the object name.
        OXYGEN_GFX_API Renderer(
            std::string_view name,
            std::weak_ptr<Graphics> gfx_weak,
            std::weak_ptr<Surface> surface_weak,
            uint32_t frames_in_flight = oxygen::kFrameBufferCount - 1);

        OXYGEN_GFX_API ~Renderer() override;

        OXYGEN_MAKE_NON_COPYABLE(Renderer);
        OXYGEN_DEFAULT_MOVABLE(Renderer);

        OXYGEN_GFX_API void Submit(FrameRenderTask task);
        OXYGEN_GFX_API void Stop();

        /**
         * Acquire a command recorder for immediate use with automatic return to pool.
         * Uses RAII with a custom deleter to automatically return the command list to the pool.
         *
         * @param role The queue role for this command list.
         * @param name Name for debugging purposes.
         * @return A unique_ptr to CommandRecorder with custom deleter for automatic cleanup.
         */
        [[nodiscard]] OXYGEN_GFX_API auto AcquireCommandRecorder(
            std::string_view queue_name,
            std::string_view command_list_name)
            -> std::unique_ptr<graphics::CommandRecorder, std::function<void(graphics::CommandRecorder*)>>;

        /**
         * Gets the index of the current frame being rendered.
         *
         * The renderer manages a set of frame buffer resources that are used to
         * render the scene. The number of frame buffers is defined by the constant
         * kFrameBufferCount. Several resources are created for each frame buffer,
         * and the index of the current frame being rendered is returned by this
         * function.
         *
         * @return The index of the current frame being rendered.
         */
        [[nodiscard]] auto CurrentFrameIndex() const { return current_frame_index_; }

        // OXYGEN_GFX_API virtual void Render(
        //     const resources::SurfaceId& surface_id,
        //     const FrameRenderTask& render_game) const;

        // virtual auto GetCommandRecorder() const -> CommandRecorderPtr = 0;

        // virtual auto GetShaderCompiler() const -> ShaderCompilerPtr = 0;
        // virtual auto GetEngineShader(std::string_view unique_id) const -> IShaderByteCodePtr = 0;

        // [[nodiscard]] virtual auto CreateWindowSurface(platform::WindowPtr weak) const -> resources::SurfaceId = 0;
        // [[nodiscard]] virtual auto CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr = 0;

    protected:
        [[nodiscard]] virtual auto CreateCommandRecorder(graphics::CommandList* command_list, graphics::CommandQueue* target_queue)
            -> std::unique_ptr<graphics::CommandRecorder>
            = 0;

        [[nodiscard]] auto GetGraphics() const noexcept -> std::shared_ptr<Graphics>;
        [[nodiscard]] auto GetSurface() const -> const Surface& { return *surface_weak_.lock(); }

        OXYGEN_GFX_API virtual auto BeginFrame() -> const RenderTarget&;
        OXYGEN_GFX_API virtual void EndFrame();

    private:
        void HandleSurfaceResize(Surface& surface);

        std::weak_ptr<Graphics> gfx_weak_;
        std::weak_ptr<Surface> surface_weak_;

        //! Holds the data to manage the frame render cycle.
        struct Frame {
            //! Synchronization timeline values for all queues involved in this
            //! cycle.
            std::unordered_map<std::string, uint64_t> timeline_values;
            //! command lists, submitted but still pending execution.
            std::vector<std::shared_ptr<CommandList>> pending_command_lists;
        };

        uint32_t frame_count_;
        std::unique_ptr<Frame[]> frames_;
        uint32_t current_frame_index_ { 0 };
    };

} // namespace graphics
} // namespace oxygen
