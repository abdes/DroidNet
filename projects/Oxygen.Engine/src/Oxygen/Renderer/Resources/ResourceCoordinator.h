//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemData.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::renderer::resources {

//! Coordinates GPU resource management for a frame.
/*!
 Processes immutable RenderItemData arrays, deduplicates and uploads resources,
 manages bindless handles and descriptor tables, and assembles the immutable
 PreparedSceneFrame for RenderGraph consumption.
*/

class ResourceCoordinator {
public:
  /*! Construct with Graphics and UploadCoordinator dependencies.
      @param graphics Weak pointer to Graphics (for DescriptorAllocator,
     ResourceRegistry, etc.)
      @param uploader observer_ptr to UploadCoordinator (non-owning, lifetime
     guaranteed by Renderer)
  */
  ResourceCoordinator(std::weak_ptr<Graphics> graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader);
  ~ResourceCoordinator();
  ResourceCoordinator(const ResourceCoordinator&) = delete;
  auto operator=(const ResourceCoordinator&) -> ResourceCoordinator& = delete;

  /*! Main entry: process all RenderItemData for the frame.
      - Deduplicates geometry, materials, transforms
      - Schedules uploads and registers views
      - Assembles and freezes PreparedSceneFrame
      @param items RenderItemData for the frame
  */
  auto ProcessPreparedSceneData(
    const std::vector<engine::sceneprep::RenderItemData>& items) -> void;

  //! Get the prepared scene frame for RenderGraph consumption.
  auto GetPreparedSceneFrame() const -> const engine::PreparedSceneFrame&;

private:
  std::weak_ptr<Graphics>
    graphics_; //!< Non-owning, lifetime managed by Renderer/Engine
  observer_ptr<engine::upload::UploadCoordinator>
    uploader_; //!< Non-owning, lifetime managed by Renderer
  std::unique_ptr<GeometryUploader> geometry_uploader_; //!< Owned
  std::unique_ptr<MaterialBinder> material_binder_; //!< Owned
  std::unique_ptr<TransformUploader> transform_uploader_; //!< Owned
  engine::PreparedSceneFrame prepared_scene_frame_;
  // Internal: SoA buffers, mappings, etc. as needed
};

} // namespace oxygen::renderer::resources
