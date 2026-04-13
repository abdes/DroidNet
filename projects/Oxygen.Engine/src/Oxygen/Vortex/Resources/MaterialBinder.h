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
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Types/MaterialShadingConstants.h>
#include <Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::resources {
} // namespace oxygen::vortex::resources

namespace oxygen::vortex::upload {
class StagingProvider;
class UploadCoordinator;
} // namespace oxygen::vortex::upload

namespace oxygen::vortex::sceneprep {
struct MaterialRef;
} // namespace oxygen::vortex::sceneprep

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::vortex::resources {

class IResourceBinder;

//! Manages GPU material constants and bindless access.
/*!
 MaterialBinder stores a per-material snapshot

 (`vortex::MaterialShadingConstants`) in a GPU buffer and exposes a stable,

 shader-visible indirection via `vortex::sceneprep::MaterialHandle`.
 Material
 constants reference textures by *bindless SRV indices* obtained from
 `IResourceBinder`, so the renderer never stores raw author indices.

 ### Primary behaviors

 - **Versioned stable handles**: `GetOrAllocate()` returns a handle with

 index+generation identity for a material content key.
 - **Eviction-aware lifecycle**: material asset evictions invalidate handles and
   schedule Nexus slot release/reclaim.
 - **Dirty tracking**: material constants are tracked as dirty per-frame and
   only dirty elements are uploaded during `EnsureFrameResources()`.
 - **Bindless SRV**: `GetMaterialShadingSrvIndex()` returns the SRV index for

 the core material shading buffer once frame resources are ensured.

 ### Lifecycle (concise)

 1. `OnFrameStart(tag, slot)` advances frame state, resets dirty tracking, and
    processes queued material eviction events.
 2. `GetOrAllocate()` / `Update()` mutate CPU-side constants and mark dirty.
 3. `EnsureFrameResources()` schedules uploads for dirty elements.
 4. `GetMaterialShadingSrvIndex()` returns the bindless SRV for rendering.

 @note Texture bindings are resolved via opaque `content::ResourceKey`s held by
   `vortex::sceneprep::MaterialRef`; no locator/path assumptions leak into the
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
  OXGN_VRTX_API MaterialBinder(observer_ptr<Graphics> gfx,
    observer_ptr<oxygen::vortex::upload::UploadCoordinator> uploader,
    observer_ptr<oxygen::vortex::upload::StagingProvider> provider,
    observer_ptr<IResourceBinder> texture_binder, // texture SRV resolution
    observer_ptr<oxygen::content::IAssetLoader> asset_loader // eviction feed
  );

  OXYGEN_MAKE_NON_COPYABLE(MaterialBinder)
  OXYGEN_MAKE_NON_MOVABLE(MaterialBinder)

  OXGN_VRTX_API ~MaterialBinder();

  //! Must be called once per frame before any other operations.
  OXGN_VRTX_API auto OnFrameStart(vortex::RendererTag, oxygen::frame::Slot slot)
    -> void;

  //! Ensures all material GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any
  //! GetMaterialShadingSrvIndex() calls. Safe to call multiple times per frame
  //! - internally optimized.
  OXGN_VRTX_API auto EnsureFrameResources() -> void;

  OXGN_VRTX_API auto GetOrAllocate(
    const oxygen::vortex::sceneprep::MaterialRef& material)
    -> oxygen::vortex::sceneprep::MaterialHandle;

  //! Update an existing material handle with new material data.
  /*!
   Updates the material constants for an existing handle. If the material
   data hasn't changed, this is a no-op.

   @param handle Previously allocated material handle (index+generation)
 @param
  material New material asset data

  ### Performance Characteristics

  - Time Complexity: O(1) for unchanged materials, O(k) for updates
  - Memory: No additional allocation for unchanged materials
  - Optimization: Content comparison avoids unnecessary GPU updates

  @see GetOrAllocate, IsHandleValid
   */
  OXGN_VRTX_API auto Update(oxygen::vortex::sceneprep::MaterialHandle handle,
    std::shared_ptr<const data::MaterialAsset> material) -> void;

  //! Check if a handle is valid and current.
  OXGN_VRTX_NDAPI auto IsHandleValid(
    oxygen::vortex::sceneprep::MaterialHandle handle) const -> bool;

  //! Returns the bindless descriptor heap index for the materials SRV.
  //! Safe to call before `EnsureFrameResources()`; SRV capacity is ensured
  //! lazily, while material payload uploads still happen via
  //! `EnsureFrameResources()`.
  OXGN_VRTX_NDAPI auto GetMaterialShadingSrvIndex() const -> ShaderVisibleIndex;

  //! Returns the bindless descriptor heap index for the procedural-grid
  //! material extension SRV table.
  OXGN_VRTX_NDAPI auto GetProceduralGridMaterialsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Get read-only access to all material constants.
  OXGN_VRTX_NDAPI auto GetMaterialShadingConstants() const noexcept
    -> std::span<const oxygen::vortex::MaterialShadingConstants>;

  //! Get read-only access to all procedural-grid material constants.
  OXGN_VRTX_NDAPI auto GetProceduralGridMaterialConstants() const noexcept
    -> std::span<const oxygen::vortex::ProceduralGridMaterialConstants>;

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
  OXGN_VRTX_API auto OverrideUvTransform(const data::MaterialAsset& material,
    glm::vec2 uv_scale, glm::vec2 uv_offset) -> bool;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex::resources
