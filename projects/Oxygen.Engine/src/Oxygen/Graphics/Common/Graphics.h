// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/Types.h>
#include <Oxygen/Graphics/Common/Forward.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Platform/Types.h>

namespace oxygen {

namespace graphics {
    class IShaderByteCode;
} // namespace graphics

namespace imgui {
    class ImguiModule;
} // namespace imgui

class Graphics : public Composition {
public:
    explicit Graphics(const char* name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Graphics() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Graphics);
    OXYGEN_DEFAULT_MOVABLE(Graphics);

    [[nodiscard]] auto GetName() const noexcept -> std::string_view
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    [[nodiscard]] bool IsWithoutRenderer() const { return is_renderer_less_; }

    [[nodiscard]] virtual OXYGEN_GFX_API auto GetShader(std::string_view unique_id) const
        -> std::shared_ptr<graphics::IShaderByteCode>
        = 0;

    //! Get the renderer instance for this graphics backend.
    /*!
      \return A weak pointer to the renderer, which expires if the backend module
      gets unloaded.

      There can be only one instance of the render for a graphics backend. That
      instance is lazily created on the first call to `GetRenderer()`. This is by
      design as in certain scenarios the backend is created and used
      renderer-less.

      Once created, the instance stays alive for as long as the graphics backend
      is not shutdown. It can always be obtained by calling `GetRenderer()`.
    */
    [[nodiscard]] OXYGEN_GFX_API auto GetRenderer() const noexcept -> const graphics::Renderer*;
    [[nodiscard]] OXYGEN_GFX_API auto GetRenderer() noexcept -> graphics::Renderer*;

    [[nodiscard]] OXYGEN_GFX_API auto GetPerFrameResourceManager() const noexcept
        -> const graphics::PerFrameResourceManager&;

    [[nodiscard]]
    virtual auto CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const
        -> std::unique_ptr<imgui::ImguiModule>
        = 0;

protected:
    //     //! Initialize the graphics backend module.
    //     virtual void InitializeGraphicsBackend(const SerializedBackendConfig& props) = 0;
    //     //! Shutdown the graphics backend module.
    //     virtual void ShutdownGraphicsBackend() = 0;

    //! Create a renderer for this graphics backend.
    virtual auto CreateRenderer() -> std::unique_ptr<graphics::Renderer> = 0;

private:
    // OXYGEN_GFX_API virtual void OnInitialize(const SerializedBackendConfig& props);
    // template <typename Base, typename... CtorArgs>
    // friend class MixinInitialize; //< Allow access to OnInitialize.

    // OXYGEN_GFX_API virtual void OnShutdown();
    // template <typename Base>
    // friend class MixinShutdown; //< Allow access to OnShutdown.

    PlatformPtr platform_ {}; //< The platform abstraction layer.

    bool is_renderer_less_ { true }; //< Indicates if the backend is renderer-less.
    std::shared_ptr<graphics::Renderer> renderer_ {}; //< The renderer instance.
};

} // namespace oxygen
