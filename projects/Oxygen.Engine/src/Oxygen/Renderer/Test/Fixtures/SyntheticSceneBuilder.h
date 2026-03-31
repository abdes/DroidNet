//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawFrameBindings.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>
#include <Oxygen/Renderer/Types/ViewFrameBindings.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>

namespace oxygen::engine::testing {

//! GPU vertex layout matching the engine's 72-byte VertexData / HLSL Vertex.
struct TestVertex {
  glm::vec3 position {};
  glm::vec3 normal {};
  glm::vec2 texcoord {};
  glm::vec3 tangent {};
  glm::vec3 bitangent {};
  glm::vec4 color {};
};
static_assert(sizeof(TestVertex) == 72U);

//! Result of SyntheticSceneBuilder::Build().
struct SyntheticScene {
  PreparedSceneFrame prepared_frame {};
  ViewConstants view_constants {};

  // Owning storage for spans in prepared_frame.
  std::vector<DrawMetadata> draw_metadata {};
  std::vector<float> world_matrix_floats {};
  std::vector<PreparedSceneFrame::PartitionRange> partitions {};

  // GPU resources kept alive for the scene's lifetime.
  struct GpuBufferSlot {
    std::shared_ptr<graphics::Buffer> buffer {};
    ShaderVisibleIndex slot { kInvalidShaderVisibleIndex };
  };
  std::vector<GpuBufferSlot> vertex_buffers {};
  std::vector<GpuBufferSlot> index_buffers {};
  GpuBufferSlot draw_metadata_buffer {};
  GpuBufferSlot world_matrices_buffer {};
  GpuBufferSlot draw_frame_bindings_buffer {};
  GpuBufferSlot view_frame_bindings_buffer {};
};

//! Builds synthetic geometry and the full bindless pipeline for GPU tests.
/*!
 Usage:
 \code
   SyntheticSceneBuilder builder(fixture, *renderer);
   builder.AddTriangle(v0, v1, v2, PassMask{...}, glm::mat4{1.0F});
   auto scene = builder.Build(kViewId, kFrameSlot, kFrameSequence);
   // Then use scene.prepared_frame and scene.view_constants with
   // SetCurrentView.
 \endcode
*/
class SyntheticSceneBuilder {
public:
  SyntheticSceneBuilder(::oxygen::Graphics& graphics, Renderer& renderer,
    std::string_view debug_prefix)
    : graphics_(graphics)
    , renderer_(renderer)
    , debug_prefix_(debug_prefix)
  {
  }

  auto AddTriangle(const TestVertex& v0, const TestVertex& v1,
    const TestVertex& v2, const PassMask& mask,
    const glm::mat4& world = glm::mat4 { 1.0F }) -> SyntheticSceneBuilder&
  {
    const auto draw_index = static_cast<std::uint32_t>(draws_.size());
    draws_.push_back(DrawSpec {
      .vertices = { v0, v1, v2 },
      .indices = { 0U, 1U, 2U },
      .mask = mask,
      .world = world,
      .transform_index = draw_index,
    });
    return *this;
  }

  auto AddIndexedMesh(std::vector<TestVertex> vertices,
    std::vector<std::uint32_t> indices, const PassMask& mask,
    const glm::mat4& world = glm::mat4 { 1.0F }) -> SyntheticSceneBuilder&
  {
    const auto draw_index = static_cast<std::uint32_t>(draws_.size());
    draws_.push_back(DrawSpec {
      .vertices = std::move(vertices),
      .indices = std::move(indices),
      .mask = mask,
      .world = world,
      .transform_index = draw_index,
    });
    return *this;
  }

  auto Build(const ViewId view_id, const frame::Slot frame_slot,
    const frame::SequenceNumber frame_sequence,
    const ResolvedView& resolved_view) -> SyntheticScene
  {
    CHECK_F(!draws_.empty(), "SyntheticSceneBuilder: no draws added");

    SyntheticScene scene {};
    UploadGeometry(scene);
    BuildDrawMetadata(scene);
    UploadTransforms(scene);
    PublishBindings(scene, view_id, frame_slot, frame_sequence);
    AssemblePreparedFrame(scene);
    BuildViewConstants(scene, resolved_view, frame_slot, frame_sequence);
    return scene;
  }

private:
  struct DrawSpec {
    std::vector<TestVertex> vertices {};
    std::vector<std::uint32_t> indices {};
    PassMask mask {};
    glm::mat4 world { 1.0F };
    std::uint32_t transform_index { 0U };
  };

  auto UploadGeometry(SyntheticScene& scene) -> void
  {
    for (std::size_t i = 0; i < draws_.size(); ++i) {
      const auto& draw = draws_[i];
      const auto label = debug_prefix_ + ".vtx." + std::to_string(i);
      auto vtx = CreateStructuredSrvBuffer(
        std::span<const TestVertex>(draw.vertices), label);
      scene.vertex_buffers.push_back(std::move(vtx));

      const auto idx_label = debug_prefix_ + ".idx." + std::to_string(i);
      auto idx = CreateRawIndexBuffer(
        std::span<const std::uint32_t>(draw.indices), idx_label);
      scene.index_buffers.push_back(std::move(idx));
    }
  }

  auto BuildDrawMetadata(SyntheticScene& scene) -> void
  {
    scene.draw_metadata.reserve(draws_.size());
    for (std::size_t i = 0; i < draws_.size(); ++i) {
      const auto& draw = draws_[i];
      scene.draw_metadata.push_back(DrawMetadata {
        .vertex_buffer_index = scene.vertex_buffers[i].slot,
        .index_buffer_index = scene.index_buffers[i].slot,
        .first_index = 0U,
        .base_vertex = 0,
        .is_indexed = 1U,
        .instance_count = 1U,
        .index_count = static_cast<std::uint32_t>(draw.indices.size()),
        .vertex_count = 0U,
        .material_handle = 0U,
        .transform_index = draw.transform_index,
        .instance_metadata_buffer_index = 0U,
        .instance_metadata_offset = 0U,
        .flags = draw.mask,
        .transform_generation = static_cast<std::uint32_t>(i + 1U),
        .submesh_index = 0U,
        .primitive_flags = 0U,
      });
    }

    const auto label = debug_prefix_ + ".draws";
    scene.draw_metadata_buffer = CreateStructuredSrvBuffer(
      std::span<const DrawMetadata>(
        scene.draw_metadata.data(), scene.draw_metadata.size()),
      label);
  }

  auto UploadTransforms(SyntheticScene& scene) -> void
  {
    scene.world_matrix_floats.resize(draws_.size() * 16U);
    for (std::size_t i = 0; i < draws_.size(); ++i) {
      std::memcpy(scene.world_matrix_floats.data() + i * 16U, &draws_[i].world,
        sizeof(glm::mat4));
    }

    const auto label = debug_prefix_ + ".worlds";
    scene.world_matrices_buffer = CreateStructuredSrvBuffer(
      std::span<const glm::mat4>(
        reinterpret_cast<const glm::mat4*>(scene.world_matrix_floats.data()),
        draws_.size()),
      label);
  }

  auto PublishBindings(SyntheticScene& scene, const ViewId view_id,
    const frame::Slot frame_slot, const frame::SequenceNumber frame_sequence)
    -> void
  {
    static_cast<void>(view_id);
    static_cast<void>(frame_slot);
    static_cast<void>(frame_sequence);

    const auto draw_frame_bindings = DrawFrameBindings {
      .draw_metadata_slot
      = BindlessDrawMetadataSlot(scene.draw_metadata_buffer.slot),
      .transforms_slot = BindlessWorldsSlot(scene.world_matrices_buffer.slot),
    };
    scene.draw_frame_bindings_buffer = CreateStructuredSrvBuffer(
      std::span<const DrawFrameBindings>(&draw_frame_bindings, 1U),
      debug_prefix_ + ".DrawFrameBindings");
    draw_frame_slot_ = scene.draw_frame_bindings_buffer.slot;
    CHECK_F(draw_frame_slot_.IsValid(),
      "SyntheticSceneBuilder: DrawFrameBindings publication failed");

    const auto view_frame_bindings
      = ViewFrameBindings { .draw_frame_slot = draw_frame_slot_ };
    scene.view_frame_bindings_buffer = CreateStructuredSrvBuffer(
      std::span<const ViewFrameBindings>(&view_frame_bindings, 1U),
      debug_prefix_ + ".ViewFrameBindings");
    view_frame_slot_ = scene.view_frame_bindings_buffer.slot;
    CHECK_F(view_frame_slot_.IsValid(),
      "SyntheticSceneBuilder: ViewFrameBindings publication failed");
  }

  auto AssemblePreparedFrame(SyntheticScene& scene) -> void
  {
    // Build partitions by grouping draws with matching masks.
    // Simple approach: one partition per unique mask, in draw order.
    struct MaskRange {
      PassMask mask {};
      std::uint32_t begin { 0U };
      std::uint32_t end { 0U };
    };
    std::vector<MaskRange> ranges;
    for (std::uint32_t i = 0; i < draws_.size(); ++i) {
      if (ranges.empty() || !(ranges.back().mask == draws_[i].mask)) {
        ranges.push_back(
          MaskRange { .mask = draws_[i].mask, .begin = i, .end = i + 1U });
      } else {
        ranges.back().end = i + 1U;
      }
    }

    scene.partitions.reserve(ranges.size());
    for (const auto& range : ranges) {
      scene.partitions.push_back(PreparedSceneFrame::PartitionRange {
        .pass_mask = range.mask,
        .begin = range.begin,
        .end = range.end,
      });
    }

    auto& pf = scene.prepared_frame;
    pf.draw_metadata_bytes = std::as_bytes(std::span(scene.draw_metadata));
    pf.world_matrices = std::span<const float>(
      scene.world_matrix_floats.data(), scene.world_matrix_floats.size());
    pf.partitions = std::span(scene.partitions);
    pf.bindless_worlds_slot = scene.world_matrices_buffer.slot;
    pf.bindless_draw_metadata_slot = scene.draw_metadata_buffer.slot;
  }

  auto BuildViewConstants(SyntheticScene& scene,
    const ResolvedView& resolved_view, const frame::Slot frame_slot,
    const frame::SequenceNumber frame_sequence) -> void
  {
    scene.view_constants.SetViewMatrix(resolved_view.ViewMatrix())
      .SetProjectionMatrix(resolved_view.ProjectionMatrix())
      .SetCameraPosition(resolved_view.CameraPosition())
      .SetTimeSeconds(0.0F, ViewConstants::kRenderer)
      .SetFrameSlot(frame_slot, ViewConstants::kRenderer)
      .SetFrameSequenceNumber(frame_sequence, ViewConstants::kRenderer)
      .SetBindlessViewFrameBindingsSlot(
        BindlessViewFrameBindingsSlot(view_frame_slot_),
        ViewConstants::kRenderer);
  }

  // -- GPU buffer helpers (adapted from VsmStageGpuHarness) --

  template <typename T>
  auto CreateStructuredSrvBuffer(std::span<const T> elements,
    std::string_view label) -> SyntheticScene::GpuBufferSlot
  {
    CHECK_F(!elements.empty(), "Structured SRV buffer requires elements");

    auto buffer = CreateAndRegisterBuffer(graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(elements.size_bytes()),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(label),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", label);
    UploadBufferBytes(buffer, elements.data(), elements.size_bytes(), label);

    auto& allocator = graphics_.GetDescriptorAllocator();
    auto handle
      = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate SRV for `{}`", label);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const graphics::BufferViewDescription view_desc {
      .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .range = graphics::BufferRange { 0U,
        static_cast<std::uint64_t>(elements.size_bytes()) },
      .stride = static_cast<std::uint32_t>(sizeof(T)),
    };

    auto view = graphics_.GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register SRV for `{}`", label);

    return SyntheticScene::GpuBufferSlot { .buffer = std::move(buffer),
      .slot = slot };
  }

  auto CreateRawIndexBuffer(std::span<const std::uint32_t> indices,
    std::string_view label) -> SyntheticScene::GpuBufferSlot
  {
    CHECK_F(!indices.empty(), "Index buffer requires elements");

    auto buffer = CreateAndRegisterBuffer(graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(indices.size_bytes()),
      .usage = graphics::BufferUsage::kStorage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(label),
    });
    CHECK_NOTNULL_F(buffer.get(), "Failed to create buffer `{}`", label);
    UploadBufferBytes(buffer, indices.data(), indices.size_bytes(), label);

    auto& allocator = graphics_.GetDescriptorAllocator();
    auto handle = allocator.Allocate(graphics::ResourceViewType::kRawBuffer_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(handle.IsValid(), "Failed to allocate SRV for `{}`", label);

    const auto slot = allocator.GetShaderVisibleIndex(handle);
    const graphics::BufferViewDescription view_desc {
      .view_type = graphics::ResourceViewType::kRawBuffer_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = Format::kR32UInt,
      .range = graphics::BufferRange { 0U,
        static_cast<std::uint64_t>(indices.size_bytes()) },
    };

    auto view = graphics_.GetResourceRegistry().RegisterView(
      *buffer, std::move(handle), view_desc);
    CHECK_F(view->IsValid(), "Failed to register SRV for `{}`", label);

    return SyntheticScene::GpuBufferSlot { .buffer = std::move(buffer),
      .slot = slot };
  }

  auto CreateAndRegisterBuffer(const graphics::BufferDesc& desc)
    -> std::shared_ptr<graphics::Buffer>
  {
    auto buffer = graphics_.CreateBuffer(desc);
    CHECK_NOTNULL_F(
      buffer.get(), "Failed to create buffer `{}`", desc.debug_name);
    auto& registry = graphics_.GetResourceRegistry();
    if (!registry.Contains(*buffer)) {
      registry.Register(buffer);
    }
    return buffer;
  }

  auto UploadBufferBytes(const std::shared_ptr<graphics::Buffer>& dest,
    const void* data, const std::size_t size_bytes, std::string_view label)
    -> void
  {
    auto upload = CreateAndRegisterBuffer(graphics::BufferDesc {
      .size_bytes = static_cast<std::uint64_t>(size_bytes),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string(label) + "_Upload",
    });
    CHECK_NOTNULL_F(upload.get(), "Failed to create upload buffer");
    upload->Update(data, size_bytes, 0U);

    const auto queue_key
      = graphics_.QueueKeyFor(graphics::QueueRole::kGraphics);
    {
      auto recorder = graphics_.AcquireCommandRecorder(
        queue_key, std::string(label) + ".Upload", true);
      CHECK_NOTNULL_F(recorder.get());
      recorder->BeginTrackingResourceState(
        *upload, graphics::ResourceStates::kGenericRead);
      recorder->BeginTrackingResourceState(
        *dest, graphics::ResourceStates::kCommon);
      recorder->RequireResourceState(
        *upload, graphics::ResourceStates::kCopySource);
      recorder->RequireResourceState(
        *dest, graphics::ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*dest, 0U, *upload, 0U, size_bytes);
      recorder->RequireResourceStateFinal(
        *dest, graphics::ResourceStates::kCommon);
    }
    auto queue = graphics_.GetCommandQueue(graphics::QueueRole::kGraphics);
    CHECK_NOTNULL_F(queue.get());
    queue->Flush();
  }

  ::oxygen::Graphics& graphics_;
  Renderer& renderer_;
  std::string debug_prefix_;
  std::vector<DrawSpec> draws_ {};
  ShaderVisibleIndex draw_frame_slot_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex view_frame_slot_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::engine::testing
