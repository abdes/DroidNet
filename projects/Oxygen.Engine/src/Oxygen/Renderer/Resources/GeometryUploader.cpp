//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "GeometryUploader.h"
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

GeometryUploader::GeometryUploader(std::weak_ptr<Graphics> graphics,
  observer_ptr<engine::upload::UploadCoordinator> uploader)
  : graphics_(std::move(graphics))
  , uploader_(uploader)
{
}

GeometryUploader::~GeometryUploader() = default;

auto GeometryUploader::ProcessMeshes(
  const std::vector<const data::Mesh*>& meshes)
  -> std::unordered_map<const data::Mesh*, MeshGpuResourceIndices>
{
  // Deduplicate meshes and assign slots
  std::unordered_map<const data::Mesh*, uint32_t> mesh_to_slot;
  std::vector<const data::Mesh*> unique_meshes;
  for (const auto* mesh : meshes) {
    if (!mesh) {
      continue;
    }
    if (!mesh_to_slot.contains(mesh)) {
      uint32_t slot = static_cast<uint32_t>(unique_meshes.size());
      mesh_to_slot[mesh] = slot;
      unique_meshes.push_back(mesh);
    }
  }

  auto graphics = graphics_.lock();
  if (!graphics) {
    return {};
  }
  auto& allocator = graphics->GetDescriptorAllocator();
  auto& registry = graphics->GetResourceRegistry();

  // For each unique mesh, create vertex and index buffers, upload, and register
  // SRVs
  std::unordered_map<const data::Mesh*, MeshGpuResourceIndices> result;
  for (const auto* mesh : unique_meshes) {
    if (!mesh) {
      continue;
    }
    MeshGpuResourceIndices indices;

    // Vertex buffer
    const uint64_t vertex_buffer_size
      = mesh->VertexCount() * sizeof(data::Vertex);
    graphics::BufferDesc v_desc;
    v_desc.size_bytes = vertex_buffer_size;
    v_desc.usage = graphics::BufferUsage::kVertex;
    v_desc.memory = graphics::BufferMemory::kDeviceLocal;
    v_desc.debug_name = "VertexBuffer";
    indices.vertex_buffer = graphics->CreateBuffer(v_desc);

    // Index buffer
    const uint64_t index_buffer_size
      = mesh->IndexCount() * sizeof(std::uint32_t); // Assuming 32-bit indices
    graphics::BufferDesc i_desc;
    i_desc.size_bytes = index_buffer_size;
    i_desc.usage = graphics::BufferUsage::kIndex;
    i_desc.memory = graphics::BufferMemory::kDeviceLocal;
    i_desc.debug_name = "IndexBuffer";
    indices.index_buffer = graphics->CreateBuffer(i_desc);

    // Upload vertex data
    engine::upload::UploadRequest v_req;
    v_req.kind = engine::upload::UploadKind::kBuffer;
    v_req.desc = engine::upload::UploadBufferDesc {
      .dst = indices.vertex_buffer,
      .size_bytes = vertex_buffer_size,
      .dst_offset = 0,
    };
    v_req.data = engine::upload::UploadDataView { std::as_bytes(
      std::span<const data::Vertex>(
        mesh->Vertices().data(), mesh->VertexCount())) };
    if (uploader_) {
      uploader_->Submit(v_req);
    }

    // Upload index data
    engine::upload::UploadRequest i_req;
    i_req.kind = engine::upload::UploadKind::kBuffer;
    i_req.desc = engine::upload::UploadBufferDesc {
      .dst = indices.index_buffer,
      .size_bytes = index_buffer_size,
      .dst_offset = 0,
    };
    {
      auto ib_view = mesh->IndexBuffer();
      i_req.data = engine::upload::UploadDataView { ib_view.bytes };
    }
    if (uploader_) {
      uploader_->Submit(i_req);
    }

    // Register buffers and SRVs
    registry.Register(indices.vertex_buffer);
    registry.Register(indices.index_buffer);

    // Vertex buffer SRV
    graphics::BufferViewDescription v_view_desc;
    v_view_desc.view_type = graphics::ResourceViewType::kTypedBuffer_SRV;
    v_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    v_view_desc.range = { 0, vertex_buffer_size };
    v_view_desc.format = {}; // Set as needed
    v_view_desc.stride = sizeof(data::Vertex);
    auto v_handle
      = allocator.Allocate(graphics::ResourceViewType::kTypedBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    indices.vertex_srv_index = allocator.GetShaderVisibleIndex(v_handle).get();
    registry.RegisterView(
      *indices.vertex_buffer, std::move(v_handle), v_view_desc);

    // Index buffer SRV
    graphics::BufferViewDescription i_view_desc;
    i_view_desc.view_type = graphics::ResourceViewType::kTypedBuffer_SRV;
    i_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    i_view_desc.range = { 0, index_buffer_size };
    i_view_desc.format = {}; // Set as needed
    // Index stride: 2 for uint16, 4 for uint32, 0 for none
    {
      auto ib_view = mesh->IndexBuffer();
      i_view_desc.stride = static_cast<uint32_t>(ib_view.ElementSize());
    }
    auto i_handle
      = allocator.Allocate(graphics::ResourceViewType::kTypedBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    indices.index_srv_index = allocator.GetShaderVisibleIndex(i_handle).get();
    registry.RegisterView(
      *indices.index_buffer, std::move(i_handle), i_view_desc);

    result[mesh] = indices;
  }

  // Map input meshes to their GPU resource indices
  std::unordered_map<const data::Mesh*, MeshGpuResourceIndices> final_result;
  for (const auto* mesh : meshes) {
    if (!mesh) {
      continue;
    }
    final_result[mesh] = result[mesh];
  }
  return final_result;
}

} // namespace oxygen::renderer::resources
