//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/Detail/WorkPayloadStore.h>

#include <Oxygen/Base/Logging.h>

namespace oxygen::content::import::detail {

namespace {

  template <typename Payload>
  [[nodiscard]] auto StorePayload(
    std::vector<std::unique_ptr<Payload>>& storage, Payload payload)
    -> WorkPayloadHandle
  {
    storage.emplace_back(std::make_unique<Payload>(std::move(payload)));
    return WorkPayloadHandle { storage.back().get() };
  }

  template <typename Payload>
  [[nodiscard]] auto RequirePayload(
    WorkPayloadHandle handle, const PlanItemKind expected) -> Payload&
  {
    auto* header = static_cast<WorkPayloadHeader*>(handle.get());
    CHECK_F(header != nullptr, "WorkPayloadHandle is null");
    CHECK_F(header->kind == expected,
      "WorkPayloadHandle kind mismatch: expected {} got {}", expected,
      header->kind);
    return *static_cast<Payload*>(handle.get());
  }

} // namespace

auto WorkPayloadStore::Store(TexturePipeline::WorkItem item)
  -> WorkPayloadHandle
{
  return StorePayload(
    textures_, TextureWorkPayload { .item = std::move(item) });
}

auto WorkPayloadStore::Store(BufferPipeline::WorkItem item) -> WorkPayloadHandle
{
  return StorePayload(buffers_, BufferWorkPayload { .item = std::move(item) });
}

auto WorkPayloadStore::Store(MaterialPipeline::WorkItem item)
  -> WorkPayloadHandle
{
  return StorePayload(
    materials_, MaterialWorkPayload { .item = std::move(item) });
}

auto WorkPayloadStore::Store(GeometryPipeline::WorkItem item)
  -> WorkPayloadHandle
{
  return StorePayload(
    geometries_, GeometryWorkPayload { .item = std::move(item) });
}

auto WorkPayloadStore::Store(ScenePipeline::WorkItem item) -> WorkPayloadHandle
{
  return StorePayload(scenes_, SceneWorkPayload { .item = std::move(item) });
}

auto WorkPayloadStore::Texture(WorkPayloadHandle handle) -> TextureWorkPayload&
{
  return RequirePayload<TextureWorkPayload>(
    handle, PlanItemKind::kTextureResource);
}

auto WorkPayloadStore::Buffer(WorkPayloadHandle handle) -> BufferWorkPayload&
{
  return RequirePayload<BufferWorkPayload>(
    handle, PlanItemKind::kBufferResource);
}

auto WorkPayloadStore::Material(WorkPayloadHandle handle)
  -> MaterialWorkPayload&
{
  return RequirePayload<MaterialWorkPayload>(
    handle, PlanItemKind::kMaterialAsset);
}

auto WorkPayloadStore::Geometry(WorkPayloadHandle handle)
  -> GeometryWorkPayload&
{
  return RequirePayload<GeometryWorkPayload>(
    handle, PlanItemKind::kGeometryAsset);
}

auto WorkPayloadStore::Scene(WorkPayloadHandle handle) -> SceneWorkPayload&
{
  return RequirePayload<SceneWorkPayload>(handle, PlanItemKind::kSceneAsset);
}

} // namespace oxygen::content::import::detail
