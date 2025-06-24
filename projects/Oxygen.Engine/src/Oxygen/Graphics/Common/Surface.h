//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Platform/Window.h>

namespace oxygen::graphics {

class Texture;
class RenderController;

//! Represents a rendering surface, such as a window or off-screen target.
/*
 A Surface defines a region where rendering occurs, which may be a window, a
 texture, or any other target that can be rendered. For off-screen rendering,
 the surface does not have an associated swapchain, and its output is not
 presented directly to the display (e.g., for shadow maps, reflection maps, or
 post-processing).

 The surface becomes usable only after being attached to a renderer. Resource
 allocation is deferred until this attachment, ensuring that GPU resources are
 created, managed, and destroyed in sync with the renderer frame lifecycle.
 This design enables correct synchronization, efficient resource reuse, and
 proper cleanup.

 Multiple surfaces can be used in parallel, each with its own renderer and
 rendering context, supporting independent rendering pipelines.
*/
class Surface : public Composition, public Named {
public:
  OXYGEN_GFX_API ~Surface() override;

  OXYGEN_DEFAULT_COPYABLE(Surface)
  OXYGEN_DEFAULT_MOVABLE(Surface)

  virtual auto AttachRenderer(std::shared_ptr<RenderController> renderer)
    -> void
    = 0;
  virtual auto DetachRenderer() -> void = 0;

  auto ShouldResize(const bool flag) -> void { should_resize_ = flag; }
  auto ShouldResize() const -> bool { return should_resize_; }

  //! Handle a surface resize.
  virtual auto Resize() -> void = 0;

  virtual auto GetCurrentBackBufferIndex() const -> uint32_t = 0;
  virtual auto GetCurrentBackBuffer() const -> std::shared_ptr<Texture> = 0;
  virtual auto GetBackBuffer(uint32_t index) const -> std::shared_ptr<Texture>
    = 0;

  //! Present the current frame if the surface supports it.
  virtual auto Present() const -> void = 0;

  [[nodiscard]] virtual auto Width() const -> uint32_t = 0;
  [[nodiscard]] virtual auto Height() const -> uint32_t = 0;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return GetComponent<ObjectMetaData>().GetName();
  }

  auto SetName(std::string_view name) noexcept -> void override
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
  class WindowComponent final : public Component {
    OXYGEN_COMPONENT(WindowComponent)
  public:
    using NativeHandles = platform::window::NativeHandles;

    explicit WindowComponent(platform::WindowPtr window)
      : window_(std::move(window))
    {
    }

    ~WindowComponent() override = default;

    OXYGEN_DEFAULT_COPYABLE(WindowComponent)
    OXYGEN_DEFAULT_MOVABLE(WindowComponent)

    [[nodiscard]] auto IsValid() const { return !window_.expired(); }

    [[nodiscard]] OXYGEN_GFX_API auto Width() const -> uint32_t;
    [[nodiscard]] OXYGEN_GFX_API auto Height() const -> uint32_t;

    [[nodiscard]] OXYGEN_GFX_API auto FrameBufferSize() const
      -> platform::window::ExtentT;

    [[nodiscard]] OXYGEN_GFX_API auto Native() const -> NativeHandles;

    [[nodiscard]] OXYGEN_GFX_API auto GetWindowTitle() const -> std::string;

  private:
    platform::WindowPtr window_;
  };

  //! Represents a surface associated with a window.
  class WindowSurface : public Surface {
  public:
    OXYGEN_GFX_API ~WindowSurface() override = default;

    OXYGEN_DEFAULT_COPYABLE(WindowSurface)
    OXYGEN_DEFAULT_MOVABLE(WindowSurface)

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
