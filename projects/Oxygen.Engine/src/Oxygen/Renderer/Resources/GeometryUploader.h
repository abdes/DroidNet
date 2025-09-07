//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

//===----------------------------------------------------------------------===//
// GeometryUploader: Deduplicates meshes, creates buffers, registers SRVs,
// schedules uploads
//===----------------------------------------------------------------------===//

//! Result of geometry upload: GPU buffer and SRV indices for a mesh
struct MeshGpuResourceIndices {
  std::shared_ptr<graphics::Buffer> vertex_buffer;
  std::shared_ptr<graphics::Buffer> index_buffer;
  uint32_t vertex_srv_index = 0;
  uint32_t index_srv_index = 0;
};

//! Deduplicates geometry assets, creates/upload buffers, registers SRVs.
class GeometryUploader {
public:
  /*! Construct with Graphics and UploadCoordinator dependencies.
      @param graphics Weak pointer to Graphics (for DescriptorAllocator,
     ResourceRegistry, etc.)
      @param uploader observer_ptr to UploadCoordinator (non-owning, lifetime
     guaranteed by Renderer)
  */
  GeometryUploader(std::weak_ptr<Graphics> graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader);
  ~GeometryUploader();
  GeometryUploader(const GeometryUploader&) = delete;
  auto operator=(const GeometryUploader&) -> GeometryUploader& = delete;

  /*! Process a batch of meshes: deduplicate, create/upload buffers, register
     SRVs.
      @param meshes Mesh pointers to process
      @return Map from mesh pointer to GPU resource indices
  */
  auto ProcessMeshes(const std::vector<const data::Mesh*>& meshes)
    -> std::unordered_map<const data::Mesh*, MeshGpuResourceIndices>;

private:
  std::weak_ptr<Graphics> graphics_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
};

} // namespace oxygen::renderer::resources
