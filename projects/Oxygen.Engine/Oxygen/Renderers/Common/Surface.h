//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinDisposable.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/Resource.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace sigslot {
  class connection; // Used for window event handling
}

namespace oxygen::renderer {

  //! Represents an area where rendering occurs.
  /*!
    A surface is a region where rendering occurs. It can be a window, a texture, or any other
    rendering target. When used for off-screen rendering, the output is not directly presented to
    the display, and therefore, the surface does not have an associated swapchain. Examples of such
    usage include shadow maps, reflection maps, and post-processing effects.
   */
  class Surface
    : public Resource<resources::kSurface>
    , public Mixin<Surface
    , Curry<MixinNamed, const char*>::mixin
    , MixinDisposable
    , MixinInitialize // last to consume remaining args
    >
  {
  public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit Surface(const resources::SurfaceId& surface_id,
                               Args &&...args) requires (sizeof...(args) > 0)
      : Resource(surface_id), Mixin(std::forward<Args>(args)...)
    {
    }
    constexpr explicit Surface(const resources::SurfaceId& surface_id)
      : Resource(surface_id), Mixin("Surface")
    {
    }

    Surface() // Create a Surface with an invalid id
      : Resource(), Mixin("Surface")
    {
    }

    ~Surface() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Surface);
    OXYGEN_MAKE_NON_MOVEABLE(Surface);

    virtual void Resize(int width, int height) = 0;
    virtual void Present() const = 0;

    [[nodiscard]] virtual auto Width() const->uint32_t = 0;
    [[nodiscard]] virtual auto Height() const->uint32_t = 0;


  protected:
    virtual void InitializeSurface() = 0;
    virtual void ReleaseSurface() noexcept = 0;

  private:
    void OnInitialize()
    {
      if (this->self().ShouldRelease())
      {
        const auto msg = fmt::format("{} OnInitialize() called twice without calling Release()", this->self().ObjectName());
        LOG_F(ERROR, "{}", msg);
        throw std::runtime_error(msg);
      }
      try {
        InitializeSurface();
        DLOG_F(INFO, "{} initialized", this->self().ObjectName());
        this->self().ShouldRelease(true);
      }
      catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to initialize {}: {}", this->self().ObjectName(), e.what());
        throw;
      }
    }
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    void OnRelease() noexcept
    {
      ReleaseSurface();
      this->self().IsInitialized(false);
    }
    template <typename Base>
    friend class MixinDisposable; //< Allow access to OnRelease.
  };

  //! Represents a surface that is associated with a window.
  /*!
    A `WindowSurface` has a swapchain, which size typically corresponds to the entire surface of the
    window. The swapchain is used to present the rendered image to the display. Its lifetime is
    strictly tied to the window lifetime.

    The swapchain is created during the initialization of the window surface and is destroyed when
    the window surface is released. Additionally, the window surface listens to window events, such
    as resizing, minimizing, and triggers a resize for the swapchain when needed.
   */
  class WindowSurface : public Surface
  {
  public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    explicit WindowSurface(const resources::SurfaceId& surface_id,
                           platform::WindowPtr window,
                           Args &&...args) requires (sizeof...(args) > 0)
      : Mixin(std::forward<Args>(args)...)
      , Surface(surface_id)
      , window_(std::move(window))
    {
    }
    explicit WindowSurface(const resources::SurfaceId& surface_id,
                           platform::WindowPtr window)
      : Surface(surface_id), window_(std::move(window))
    {
    }

    WindowSurface() // Create a WindowSurface with an invalid id
      : Surface()
    {
    }

    OXYGEN_API ~WindowSurface() override = default;

    OXYGEN_MAKE_NON_COPYABLE(WindowSurface);
    OXYGEN_MAKE_NON_MOVEABLE(WindowSurface);

    //! Request the surface swapchain to be presented to the display.
    void Present() const override = 0;

    OXYGEN_API [[nodiscard]] auto Width() const->uint32_t override;
    OXYGEN_API [[nodiscard]] auto Height() const->uint32_t override;

  protected:
    //! Initialize the window surface.
    /*!
      This method is called when the surface is being initialized. It is responsible for setting
      up the swapchain and listening to window events. The base class implementation connects the
      window events to handlers that will request swapchain resizes, and must be always called by
      derived classes.
     */
    OXYGEN_API void InitializeSurface() override;

    //! Release the window surface.
    /*!
      This method is called when the surface is being released. It is responsible for releasing the
      swapchain and disconnecting from window events. The base class implementation releases the
      swapchain and must be always called by derived classes.
     */
    OXYGEN_API void ReleaseSurface() noexcept override;

  private:
    platform::WindowPtr window_;

    std::unique_ptr<sigslot::connection> on_resize_;
    std::unique_ptr<sigslot::connection> on_minimized_;
    std::unique_ptr<sigslot::connection> on_restored_;
    std::unique_ptr<sigslot::connection> on_close_;
  };

}  // namespace oxygen::renderer
