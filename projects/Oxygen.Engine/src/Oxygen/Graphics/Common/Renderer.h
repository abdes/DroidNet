//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "./api_export.h"
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/Types/EngineResources.h>
#include <Oxygen/Graphics/Common/Types/RenderGameFunction.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

namespace imgui {
    class ImguiModule;
} // namespace imgui

// TODO: DELETE
///**
// * Rendering device information.
// */
// struct DeviceInfo {
//  std::string description; //< GPU name.
//  std::string misc; //< Miscellaneous GPU info.
//  std::vector<std::string> features; //< Supported graphics features.
//};

namespace graphics {

    /**
     * Base class for all renderers.
     *
     * This is the interface that allows to interact with the graphics API to
     * create resources, record commands and execute them. The backend
     * implementation is dynamically loaded and initialized via the renderer
     * loader.
     *
     * It is possible to have multiple renderers active at the same time, but in
     * most cases, only one is needed, and that one can be obtained at any time
     * using the GetRenderer() function from the loader.
     */
    class Renderer : public Composition {
    public:
        explicit Renderer()
            : Renderer("Renderer")
        {
        }

        //! Default constructor, sets the object name.
        Renderer(std::string_view name)
        {
            AddComponent<ObjectMetaData>(name);
        }

        OXYGEN_GFX_API ~Renderer() override = default;

        OXYGEN_MAKE_NON_COPYABLE(Renderer); //< Non-copyable.
        OXYGEN_MAKE_NON_MOVABLE(Renderer); //< Non-moveable.

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
            const RenderGameFunction& render_game) const;

        virtual auto GetCommandRecorder() const -> CommandRecorderPtr = 0;

        // virtual auto GetShaderCompiler() const -> ShaderCompilerPtr = 0;
        // virtual auto GetEngineShader(std::string_view unique_id) const -> IShaderByteCodePtr = 0;

        /**
         * Device resources creation functions
         * @{
         */

        [[nodiscard]] virtual auto CreateWindowSurface(platform::WindowPtr weak) const -> resources::SurfaceId = 0;
        [[nodiscard]] virtual auto CreateVertexBuffer(const void* data, size_t size, uint32_t stride) const -> BufferPtr = 0;

        /**@}*/

    protected:
        OXYGEN_GFX_API virtual void OnInitialize(/*PlatformPtr platform, const GraphicsConfig& props*/);
        template <typename Base, typename... CtorArgs>
        friend class MixinInitialize; //< Allow access to OnInitialize.

        OXYGEN_GFX_API virtual void OnShutdown();
        template <typename Base>
        friend class MixinShutdown; //< Allow access to OnShutdown.

        virtual auto BeginFrame(const resources::SurfaceId& surface_id) -> const RenderTarget& = 0;
        virtual void EndFrame(const resources::SurfaceId& surface_id) const = 0;

        [[nodiscard]] auto GetPlatform() const -> PlatformPtr { return platform_; }
        [[nodiscard]] auto GetInitProperties() const -> const GraphicsConfig& { return props_; }

    private:
        GraphicsConfig props_;
        PlatformPtr platform_;

        mutable uint32_t current_frame_index_ { 0 };
    };

} // namespace graphics
} // namespace oxygen
