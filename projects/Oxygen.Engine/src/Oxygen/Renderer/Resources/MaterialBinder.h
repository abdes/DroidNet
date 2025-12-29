//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>

#include <glm/vec2.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/Types/MaterialConstants.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {
} // namespace oxygen::renderer::resources

namespace oxygen::engine::upload {
class StagingProvider;
class UploadCoordinator;
} // namespace oxygen::engine::upload

namespace oxygen::engine::sceneprep {
struct MaterialRef;
} // namespace oxygen::engine::sceneprep

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::data {
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::renderer::resources {

class IResourceBinder;

//! Manages GPU material constants and bindless access.
/*!
 MaterialBinder stores a per-material snapshot (`engine::MaterialConstants`) in
 a GPU buffer and exposes a stable, shader-visible indirection via
 `engine::sceneprep::MaterialHandle`.

 Material constants reference textures by *bindless SRV indices* obtained from
 `IResourceBinder`, so the renderer never stores raw author indices.

 ### Primary behaviors

 - **Stable handles**: `GetOrAllocate()` returns a stable handle for a given
   material content key for the lifetime of the binder.
 - **Dirty tracking**: material constants are tracked as dirty per-frame and
   only dirty elements are uploaded during `EnsureFrameResources()`.
 - **Bindless SRV**: `GetMaterialsSrvIndex()` returns the SRV index for the
   material constants buffer once frame resources are ensured.

 ### Lifecycle (concise)

 1. `OnFrameStart(tag, slot)` resets per-frame dirty tracking.
 2. `GetOrAllocate()` / `Update()` mutate CPU-side constants and mark dirty.
 3. `EnsureFrameResources()` schedules uploads for dirty elements.
 4. `GetMaterialsSrvIndex()` returns the bindless SRV for rendering.

 @note Texture bindings are resolved via opaque `content::ResourceKey`s held by
   `engine::sceneprep::MaterialRef`; no locator/path assumptions leak into the
   renderer.
 @see TextureBinder for a reference binder design
 @see GeometryUploader, TransformUploader for related upload patterns
*/
class MaterialBinder {
public:
  /*!
   @note MaterialBinder lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the MaterialBinder.
  */
  OXGN_RNDR_API MaterialBinder(observer_ptr<Graphics> gfx,
    observer_ptr<oxygen::engine::upload::UploadCoordinator> uploader,
    observer_ptr<oxygen::engine::upload::StagingProvider> provider,
    observer_ptr<IResourceBinder> texture_binder);

  OXYGEN_MAKE_NON_COPYABLE(MaterialBinder)
  OXYGEN_MAKE_NON_MOVABLE(MaterialBinder)

  OXGN_RNDR_API ~MaterialBinder();

  //! Must be called once per frame before any other operations.
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

  //! Ensures all material GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any GetMaterialsSrvIndex()
  //! calls. Safe to call multiple times per frame - internally optimized.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  OXGN_RNDR_API auto GetOrAllocate(
    const oxygen::engine::sceneprep::MaterialRef& material)
    -> oxygen::engine::sceneprep::MaterialHandle;

  //! Update an existing material handle with new material data.
  /*!
   Updates the material constants for an existing handle. If the material
   data hasn't changed, this is a no-op.

   @param handle Previously allocated material handle
   @param material New material asset data

  ### Performance Characteristics

  - Time Complexity: O(1) for unchanged materials, O(k) for updates
  - Memory: No additional allocation for unchanged materials
  - Optimization: Content comparison avoids unnecessary GPU updates

  @see GetOrAllocate, IsHandleValid
   */
  OXGN_RNDR_API auto Update(oxygen::engine::sceneprep::MaterialHandle handle,
    std::shared_ptr<const data::MaterialAsset> material) -> void;

  //! Check if a handle is valid.
  [[nodiscard]] OXGN_RNDR_API auto IsHandleValid(
    oxygen::engine::sceneprep::MaterialHandle handle) const -> bool;

  //! Returns the bindless descriptor heap index for the materials SRV.
  //! REQUIRES: EnsureFrameResources() must have been called this frame.
  [[nodiscard]] OXGN_RNDR_API auto GetMaterialsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Get read-only access to all material constants.
  [[nodiscard]] OXGN_RNDR_API auto GetMaterialConstants() const noexcept
    -> std::span<const oxygen::engine::MaterialConstants>;

  //! Overrides UV scale/offset for an existing material instance.
  /*!
   Updates the shader-visible UV transform for a material already registered
   with this binder.

   This is intended for editor/runtime authoring workflows where interactive
   parameter tweaks must not require rebuilding geometry.

    TODO: This is a stopgap for examples and editor prototyping. Prefer a
    MaterialInstance system where overrides are attached to a per-object (or
    per-instance) material wrapper rather than mutating a shared MaterialAsset.

   @param material Material asset instance whose constants should be updated.
   @param uv_scale UV scale (tiling). Components must be finite and > 0.
   @param uv_offset UV offset. Components must be finite.
   @return true if the material was found and updated; false otherwise.
  */
  OXGN_RNDR_API auto OverrideUvTransform(const data::MaterialAsset& material,
    glm::vec2 uv_scale, glm::vec2 uv_offset) -> bool;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::renderer::resources
