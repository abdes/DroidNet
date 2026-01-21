//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Content/Import/Internal/ImportPlanner.h>
#include <Oxygen/Content/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>

namespace oxygen::content::import::detail {

//! Header identifying the stored work payload kind.
struct WorkPayloadHeader final {
  PlanItemKind kind = PlanItemKind::kTextureResource;
};

//! Stored payload for texture pipeline work.
struct TextureWorkPayload final {
  WorkPayloadHeader header { PlanItemKind::kTextureResource };
  TexturePipeline::WorkItem item;
};

//! Stored payload for buffer pipeline work.
struct BufferWorkPayload final {
  WorkPayloadHeader header { PlanItemKind::kBufferResource };
  BufferPipeline::WorkItem item;
};

//! Stored payload for material pipeline work.
struct MaterialWorkPayload final {
  WorkPayloadHeader header { PlanItemKind::kMaterialAsset };
  MaterialPipeline::WorkItem item;
};

//! Stored payload for geometry pipeline work.
struct GeometryWorkPayload final {
  WorkPayloadHeader header { PlanItemKind::kGeometryAsset };
  GeometryPipeline::WorkItem item;
};

//! Stored payload for scene pipeline work.
struct SceneWorkPayload final {
  WorkPayloadHeader header { PlanItemKind::kSceneAsset };
  ScenePipeline::WorkItem item;
};

//! Job-owned storage for pipeline work payloads.
class WorkPayloadStore final {
public:
  //! Store a texture work item and return a handle.
  [[nodiscard]] auto Store(TexturePipeline::WorkItem item) -> WorkPayloadHandle;

  //! Store a buffer work item and return a handle.
  [[nodiscard]] auto Store(BufferPipeline::WorkItem item) -> WorkPayloadHandle;

  //! Store a material work item and return a handle.
  [[nodiscard]] auto Store(MaterialPipeline::WorkItem item)
    -> WorkPayloadHandle;

  //! Store a geometry work item and return a handle.
  [[nodiscard]] auto Store(GeometryPipeline::WorkItem item)
    -> WorkPayloadHandle;

  //! Store a scene work item and return a handle.
  [[nodiscard]] auto Store(ScenePipeline::WorkItem item) -> WorkPayloadHandle;

  //! Get a stored texture payload.
  [[nodiscard]] auto Texture(WorkPayloadHandle handle) -> TextureWorkPayload&;

  //! Get a stored buffer payload.
  [[nodiscard]] auto Buffer(WorkPayloadHandle handle) -> BufferWorkPayload&;

  //! Get a stored material payload.
  [[nodiscard]] auto Material(WorkPayloadHandle handle) -> MaterialWorkPayload&;

  //! Get a stored geometry payload.
  [[nodiscard]] auto Geometry(WorkPayloadHandle handle) -> GeometryWorkPayload&;

  //! Get a stored scene payload.
  [[nodiscard]] auto Scene(WorkPayloadHandle handle) -> SceneWorkPayload&;

private:
  std::vector<std::unique_ptr<TextureWorkPayload>> textures_;
  std::vector<std::unique_ptr<BufferWorkPayload>> buffers_;
  std::vector<std::unique_ptr<MaterialWorkPayload>> materials_;
  std::vector<std::unique_ptr<GeometryWorkPayload>> geometries_;
  std::vector<std::unique_ptr<SceneWorkPayload>> scenes_;
};

} // namespace oxygen::content::import::detail
