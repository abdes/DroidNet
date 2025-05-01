//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Resource.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/RenderTarget.h>
#include <Oxygen/Graphics/Common/Types/EngineResources.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen::graphics {

//! Represents an area where rendering occurs.
/*!
  A surface is a region where rendering occurs. It can be a window, a
  texture, or any other rendering target. When used for off-screen
  rendering, the output is not directly presented to the display, and
  therefore, the surface does not have an associated swapchain. Examples of
  such usage include shadow maps, reflection maps, and post-processing
  effects.
 */
class Surface : public Composition, public Named, public RenderTarget {
public:
    OXYGEN_GFX_API ~Surface() override;

    OXYGEN_DEFAULT_COPYABLE(Surface);
    OXYGEN_DEFAULT_MOVABLE(Surface);

    void ShouldResize(const bool flag) { should_resize_ = flag; }
    auto ShouldResize() const -> bool { return should_resize_; }
    virtual void Resize() = 0;
    virtual void Present() const = 0;

    [[nodiscard]] virtual auto Width() const -> uint32_t = 0;
    [[nodiscard]] virtual auto Height() const -> uint32_t = 0;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }

protected:
    explicit Surface(std::string_view name = "Surface")
    {
        AddComponent<ObjectMetaData>(name);
    }

private:
    bool should_resize_ { false };
};

namespace detail {

    class WindowSurface;

    //! A component that encapsulates the window part of a WindowSurface.
    class WindowComponent : public Component {
        OXYGEN_COMPONENT(WindowComponent)
    public:
        using NativeHandles = platform::window::NativeHandles;

        OXYGEN_DEFAULT_COPYABLE(WindowComponent)
        OXYGEN_DEFAULT_MOVABLE(WindowComponent)

        [[nodiscard]] auto IsValid() const { return !window_.expired(); }

        [[nodiscard]] OXYGEN_GFX_API auto Width() const -> uint32_t;
        [[nodiscard]] OXYGEN_GFX_API auto Height() const -> uint32_t;

        [[nodiscard]] OXYGEN_GFX_API auto FrameBufferSize() const -> platform::window::ExtentT;

        [[nodiscard]] OXYGEN_GFX_API auto Native() const -> NativeHandles;

    private:
        friend WindowSurface;
        explicit WindowComponent(platform::WindowPtr window)
            : window_(std::move(window))
        {
        }

        platform::WindowPtr window_;
    };

    //! Represents a surface that is associated with a window.
    class WindowSurface : public Surface {
    public:
        OXYGEN_GFX_API ~WindowSurface() override = default;

        OXYGEN_DEFAULT_COPYABLE(WindowSurface)
        OXYGEN_DEFAULT_MOVABLE(WindowSurface)

        //! Request the surface swapchain to be presented to the display.
        void Present() const override = 0;

        [[nodiscard]] auto Width() const -> uint32_t override
        {
            return GetComponent<WindowComponent>().Width();
        }

        [[nodiscard]] auto Height() const -> uint32_t override
        {
            return GetComponent<WindowComponent>().Height();
        }

    protected:
        explicit WindowSurface(platform::WindowPtr window)
            : Surface("Window Surface")
        {
            AddComponent<WindowComponent>(std::move(window));
        }
    };
} // namespace detail

} // namespace oxygen::graphics
