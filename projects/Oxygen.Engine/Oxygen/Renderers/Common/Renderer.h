//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Platform/Types.h"
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
   * This is the interface that allows to intercat with the graphics API to
   * create resources, record commands and execute them. The backend
   * implementation is dynamically loaded and initialized via the renderer
   * loader.
   *
   * It is possible to havle multiple renderers active at the same time, but in
   * most cases, only one is needed, and that one can be obtained at any time
   * using the GetRenderer() function from the loader.
   */
  class Renderer
  {
  public:
    Renderer() = default;
    /**
     * Destructor. Calls Shutdown() if not already done.
     */
    virtual ~Renderer() { Shutdown(); }

    OXYGEN_MAKE_NON_COPYABLE(Renderer); //< Non-copyable.
    OXYGEN_MAKE_NON_MOVEABLE(Renderer); //< Non-moveable.

    /// Get a user friendly name for the renderer, for logging purposes.
    [[nodiscard]] virtual auto Name() const->std::string = 0;

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
    [[nodiscard]] virtual auto CurrentFrameIndex() const->size_t = 0;

    /**
     * Initializes the renderer.
     *
     * Initialization of the renderer involving platform-specific detection of
     * adapters, attached displays, and their capabilities, and the selection of
     * the most suitable rendering device as the main device. That main device
     * is unique for the lifetime of the renderer, and can be obtained at any
     * time after initialization and before shutdown, using the GetMainDevice()
     * method.
     *
     * @param platform The platform abstraction interface to use for the
     * renderer initialization.
     * @param props Configuration properties to customize the renderer and guide
     * the initialization process.
     *
     * @throws std::runtime_error if the initialization fails.
     */
    void Initialize(PlatformPtr platform, const RendererProperties& props);

    /**
     * Shuts down the renderer.
     *
     * This method releases all resources and cleans up the renderer state.
     * After this method is called, the renderer is in a shutdown state and
     * cannot be used anymore. The renderer can be re-initialized by calling
     * Initialize() again.
     *
     * @throws std::runtime_error if the shutdown fails. When that happens, the
     * renderer may be in an inconsistent state, and should not be used until
     * re-initialize.
     */
    void Shutdown();

    /**
     * Checks if the renderer is ready to be used.
     *
     * The renderer is ready to be used after it has been initialized and is not
     * in a shutdown state.
     *
     * @return \c true if the renderer is ready to be used, \c false otherwise.
     */
    [[nodiscard]] constexpr auto IsReady() const->bool { return is_ready_; }

    virtual void Render(const renderer::resources::SurfaceId& surface_id) = 0;

    /**
     * DeviceResourcesCreation Resources creation functions
     * @{
     */

    virtual void CreateSwapChain(const renderer::resources::SurfaceId& surface_id) const = 0;

    /**@}*/

  protected:
    virtual void OnShutdown() = 0;   //< Backend specific shutdown.
    virtual void OnInitialize() = 0; //< Backend specific initialization.

    [[nodiscard]] auto GetPlatform() const->PlatformPtr { return platform_; }
    [[nodiscard]] auto GetPInitProperties() const-> const RendererProperties& { return props_; }

  private:
    bool is_ready_{ false };

    RendererProperties props_;
    PlatformPtr platform_;
  };

  inline void Renderer::Initialize(PlatformPtr platform, const RendererProperties& props)
  {
    // If already initialized, return.
    if (IsReady()) return;

    platform_ = std::move(platform);
    props_ = props;

    try
    {
      OnInitialize();
    }
    catch (const std::exception& e)
    {
      throw std::runtime_error("renderer initialization error: " + std::string(e.what()));
    }

    is_ready_ = true;
  }

  inline void Renderer::Shutdown()
  {
    // If already shutdown, return.
    if (!IsReady()) return;

    try
    {
      OnShutdown();
    }
    catch (const std::exception& e)
    {
      throw std::runtime_error("renderer shutdown incomplete: " + std::string(e.what()));
    }

    is_ready_ = false;
  }

} // namespace oxygen
