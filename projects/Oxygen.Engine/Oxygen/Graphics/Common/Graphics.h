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
#include "Oxygen/Core/Engine.h"
#include "Oxygen/Graphics/Common/Types.h"
#include "Oxygen/Platform/Common/Types.h"

namespace oxygen::renderer {
class IShaderByteCode;
}

namespace oxygen::renderer {
class PerFrameResourceManager;
}

namespace oxygen {

struct GraphicsBackendProperties {
  //! Device selection guidance.
  /*!
    The graphics backend will try to select the most suitable GPU based on its
    capabilities, but the selection can be influenced by the following
    properties. Note that the properties are hints and if they cannot be
    satisfied, the renderer will fall back to the default behavior.

    \note The preferred_card_name and preferred_card_device_id are mutually
    exclusive, and the name must point to a valid null terminated string with
    an indefinite lifetime.
  */
  //! @{
  std::string preferred_card_name {};
  int32_t preferred_card_device_id { -1 };
  //! @}

  bool enable_debug { false }; //< Enable the backend debug layer.
  bool enable_validation { false }; //< Enable GPU validation.

  //! Optional renderer configuration properties. When not set, it indicates the
  //! the engine is running renderer-less and the graphics backend will never
  //! create a renderer instance.
  std::optional<RendererProperties> renderer_props;
};

class Graphics
  : public Mixin<Graphics,
      Curry<MixinNamed, const char*>::mixin,
      MixinInitialize,
      MixinShutdown>
{
 public:
  //! Constructor to forward the arguments to the mixins in the chain. Requires
  //! at least a name argument.
  template <typename... Args>
  constexpr explicit Graphics(const char* name, Args&&... args)
    : Mixin(name, std::forward<Args>(args)...)
  {
  }

  ~Graphics() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Graphics);
  OXYGEN_DEFAULT_MOVABLE(Graphics);

  [[nodiscard]] constexpr bool IsWithoutRenderer() const { return is_renderer_less_; }

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
  OXYGEN_API [[nodiscard]] auto GetRenderer() const noexcept -> const Renderer*;
  OXYGEN_API [[nodiscard]] auto GetRenderer() noexcept -> Renderer*;

  OXYGEN_API [[nodiscard]] auto GetPerFrameResourceManager() const noexcept -> const renderer::PerFrameResourceManager&;

 protected:
  //! Initialize the graphics backend module.
  virtual void InitializeGraphicsBackend(PlatformPtr platform, const GraphicsBackendProperties& props) = 0;
  //! Shutdown the graphics backend module.
  virtual void ShutdownGraphicsBackend() = 0;

  //! Create a renderer for this graphics backend.
  virtual auto CreateRenderer() -> std::unique_ptr<Renderer> = 0;

 private:
  OXYGEN_API virtual void OnInitialize(PlatformPtr platform, const GraphicsBackendProperties& props);
  template <typename Base, typename... CtorArgs>
  friend class MixinInitialize; //< Allow access to OnInitialize.

  OXYGEN_API virtual void OnShutdown();
  template <typename Base>
  friend class MixinShutdown; //< Allow access to OnShutdown.

  PlatformPtr platform_ {}; //< The platform abstraction layer.

  bool is_renderer_less_ { true }; //< Indicates if the backend is renderer-less.
  std::shared_ptr<Renderer> renderer_ {}; //< The renderer instance.
};

} // namespace oxygen
