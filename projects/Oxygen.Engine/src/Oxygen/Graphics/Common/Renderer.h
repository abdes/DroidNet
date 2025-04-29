//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Types/EngineResources.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Platform/Types.h>


namespace oxygen {

namespace imgui {
    class ImguiModule;
} // namespace imgui

namespace graphics {

    class Renderer : public Composition {
    public:
        Renderer(
            std::weak_ptr<Graphics> gfx_weak,
            std::shared_ptr<Surface> surface,
            uint32_t frames_in_flight = oxygen::kFrameBufferCount - 1)
            : Renderer("Renderer", std::move(gfx_weak), std::move(surface), frames_in_flight)
        {
        }

        //! Default constructor, sets the object name.
        OXYGEN_GFX_API Renderer(
            std::string_view name,
            std::weak_ptr<Graphics> gfx_weak,
            std::shared_ptr<Surface> surface,
            uint32_t frames_in_flight = oxygen::kFrameBufferCount - 1);

        OXYGEN_GFX_API ~Renderer() override;

        OXYGEN_MAKE_NON_COPYABLE(Renderer);
        OXYGEN_DEFAULT_MOVABLE(Renderer);

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
        [[nodiscard]] virtual auto CurrentFrameIndex() const -> uint32_t
        {
            return current_frame_index_;
        }

        OXYGEN_GFX_API virtual void Render(
            const resources::SurfaceId& surface_id,
            const FrameRenderTask& render_game) const;

        virtual auto GetCommandRecorder() const -> CommandRecorderPtr = 0;

        // virtual auto GetShaderCompiler() const -> ShaderCompilerPtr = 0;
        // virtual auto GetEngineShader(std::string_view unique_id) const -> IShaderByteCodePtr = 0;

        // [[nodiscard]] virtual auto CreateWindowSurface(platform::WindowPtr weak) const -> resources::SurfaceId = 0;
        // [[nodiscard]] virtual auto CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr = 0;

    protected:
        virtual auto BeginFrame(const resources::SurfaceId& surface_id) -> const RenderTarget& = 0;
        virtual void EndFrame(const resources::SurfaceId& surface_id) const = 0;

    private:
        mutable uint32_t current_frame_index_ { 0 };
    };

} // namespace graphics
} // namespace oxygen
