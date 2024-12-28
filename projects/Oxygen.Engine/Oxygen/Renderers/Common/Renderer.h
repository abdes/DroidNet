//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Platform/Types.h"
#include "Oxygen/Renderers/Common/MixinDeferredRelease.h"
#include "Oxygen/Renderers/Common/MixinRendererEvents.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen {

  /**
   * Rendering device information.
   */
  struct DeviceInfo
  {
    std::string description;                  //< GPU name.
    std::string misc;                         //< Miscellaneous GPU info.
    std::vector<std::string> features;        //< Supported graphics features.
  };

  struct RendererProperties
  {
    /**
     * Device selection guidance. The renderer will try to select the most
     * suitable GPU based on its capabilities, but the selection can be
     * influenced by the following properties. Note that the properties are
     * hints and if they cannot be satisfied, the renderer will fall back to the
     * default behavior.
     *
     * \note The preferred_card_name and preferred_card_device_id are mutually
     * exclusive, and the name must point to a valid null terminated string with
     * an indefinite lifetime.
     * @{
     */

    const char* preferred_card_name{ nullptr };
    int32_t preferred_card_device_id{ -1 };

    /**@}*/

    bool enable_debug{ false };               //< Enable debug layer.
    bool enable_validation{ false };          //< Enable GPU validation.
  };

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
  class Renderer
    : public Mixin
    < Renderer
    , Curry<MixinNamed, const char*>::mixin  //<! Named object
    , MixinShutdown                //<! Managed lifecycle
    , MixinInitialize                //<! Managed lifecycle
    , renderer::MixinRendererEvents          //<! Exposes renderer events
    , renderer::MixinDeferredRelease         //<! Handles deferred release of resources
    >
  {
  public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit Renderer(Args &&...args)
      : Mixin(std::forward<Args>(args)...)
    {
    }

    //! Default constructor, sets the object name.
    Renderer() : Mixin("Renderer")
    {
    }

    ~Renderer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Renderer); //< Non-copyable.
    OXYGEN_MAKE_NON_MOVEABLE(Renderer); //< Non-moveable.

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
    [[nodiscard]] virtual auto CurrentFrameIndex() const->uint32_t { return current_frame_index_; }

    virtual void Render(const renderer::resources::SurfaceId& surface_id);

    /**
     * Device resources creation functions
     * @{
     */

    [[nodiscard]] virtual auto CreateWindowSurface(platform::WindowPtr weak) const->renderer::SurfacePtr = 0;

    /**@}*/


  protected:

    virtual void OnInitialize(PlatformPtr platform, const RendererProperties& props);
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    virtual void OnShutdown();
    template <typename Base>
    friend class MixinShutdown; //< Allow access to OnShutdown.

    virtual void BeginFrame();
    virtual void EndFrame();
    virtual void RenderCurrentFrame(const renderer::resources::SurfaceId& surface_id) = 0;

    [[nodiscard]] auto GetPlatform() const->PlatformPtr { return platform_; }
    [[nodiscard]] auto GetPInitProperties() const-> const RendererProperties& { return props_; }

  private:
    RendererProperties props_;
    PlatformPtr platform_;

    uint32_t current_frame_index_{ 0 };
  };

  inline void Renderer::OnInitialize(PlatformPtr platform, const RendererProperties& props)
  {
    platform_ = std::move(platform);
    props_ = props;
    EmitRendererInitialized();
  }

  inline void Renderer::OnShutdown()
  {
    EmitRendererShutdown();
    platform_.reset();
  }

  inline void Renderer::BeginFrame()
  {
    DLOG_F(2, "BEGIN Frame");
    EmitBeginFrameRender(current_frame_index_);
  }

  inline void Renderer::EndFrame()
  {
    EmitEndFrameRender(current_frame_index_);
    current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
    DLOG_F(2, "END Frame");
  }

  inline void Renderer::Render(const renderer::resources::SurfaceId& surface_id)
  {
    BeginFrame();
    RenderCurrentFrame(surface_id);
    EndFrame();
  }

} // namespace oxygen
