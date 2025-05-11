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
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

namespace oxygen {

class Graphics;

namespace graphics {

    class Surface;
    class CommandList;
    class CommandQueue;
    class CommandRecorder;

    namespace detail {
        class PerFrameResourceManager;
    } // namespace detail

    class Renderer
        : public Composition,
          public std::enable_shared_from_this<Renderer> {
    public:
        OXYGEN_GFX_API Renderer(
            std::string_view name,
            std::weak_ptr<Graphics> gfx_weak,
            std::weak_ptr<Surface> surface_weak,
            uint32_t frames_in_flight = kFrameBufferCount - 1);

        OXYGEN_GFX_API ~Renderer() override;

        OXYGEN_MAKE_NON_COPYABLE(Renderer)
        OXYGEN_DEFAULT_MOVABLE(Renderer)

        OXYGEN_GFX_API void Submit(FrameRenderTask task);
        OXYGEN_GFX_API void Stop();

        [[nodiscard]] OXYGEN_GFX_API auto AcquireCommandRecorder(
            std::string_view queue_name,
            std::string_view command_list_name)
            -> std::unique_ptr<CommandRecorder, std::function<void(CommandRecorder*)>>;

        [[nodiscard]] auto CurrentFrameIndex() const { return current_frame_index_; }

        [[nodiscard]] auto GetPerFrameResourceManager() const -> detail::PerFrameResourceManager&
        {
            return *per_frame_resource_manager_;
        }

        OXYGEN_GFX_API [[nodiscard]] virtual auto CreateTexture(graphics::TextureDesc desc) const
            -> std::shared_ptr<graphics::Texture>
            = 0;

        OXYGEN_GFX_API [[nodiscard]] virtual auto CreateTextureFromNativeObject(
            TextureDesc desc, NativeObject native) const
            -> std::shared_ptr<graphics::Texture>
            = 0;

    protected:
        [[nodiscard]] virtual auto CreateCommandRecorder(
            CommandList* command_list,
            CommandQueue* target_queue)
            -> std::unique_ptr<CommandRecorder>
            = 0;

        OXYGEN_GFX_API virtual void BeginFrame();
        OXYGEN_GFX_API virtual void EndFrame();

    private:
        void HandleSurfaceResize(Surface& surface);

        std::weak_ptr<Graphics> gfx_weak_;
        std::weak_ptr<Surface> surface_weak_;

        //! Holds the data to manage the frame render cycle.
        struct Frame {
            //! Synchronization timeline values for all queues involved in this cycle.
            std::unordered_map<std::string, uint64_t> timeline_values;
            //! command lists, submitted but still pending execution.
            std::vector<std::shared_ptr<CommandList>> pending_command_lists;
        };

        uint32_t frame_count_;
        std::unique_ptr<Frame[]> frames_;
        uint32_t current_frame_index_ { 0 };

        std::shared_ptr<detail::PerFrameResourceManager> per_frame_resource_manager_;
    };

} // namespace graphics
} // namespace oxygen
