//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Content/Import/BufferImportTypes.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportPipeline.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {

//! View of mesh streams held in memory.
struct MeshStreamView {
  std::span<const glm::vec3> positions;
  std::span<const glm::vec3> normals;
  std::span<const glm::vec2> texcoords;
  std::span<const glm::vec3> tangents;
  std::span<const glm::vec3> bitangents;
  std::span<const glm::vec4> colors;
  std::span<const glm::uvec4> joint_indices;
  std::span<const glm::vec4> joint_weights;
};

//! Range of triangle indices for a submesh.
struct TriangleRange {
  uint32_t material_slot = 0;
  uint32_t first_index = 0;
  uint32_t index_count = 0;
};

//! Axis-aligned bounds for geometry.
struct Bounds3 {
  std::array<float, 3> min {};
  std::array<float, 3> max {};
};

//! Triangle mesh view.
struct TriangleMesh {
  data::MeshType mesh_type = data::MeshType::kStandard;
  MeshStreamView streams;
  std::span<const glm::mat4> inverse_bind_matrices;
  std::span<const uint32_t> joint_remap;
  std::span<const uint32_t> indices;
  std::span<const TriangleRange> ranges;
  std::optional<Bounds3> bounds;
};

//! LOD entry for a mesh source.
struct MeshLod {
  std::string lod_name;
  TriangleMesh source;
  std::shared_ptr<const void> source_owner;
};

//! Pipeline for CPU-bound geometry cooking.
/*!
 GeometryPipeline is a compute-only pipeline used by async imports. It
 assembles geometry descriptor bytes and buffer payloads on the import thread
 while offloading heavy compute to the provided `co::ThreadPool`.

 The pipeline does not perform I/O and does not assign resource indices.
 Use `BufferEmitter` and `AssetEmitter` to commit the results.

 @see CookedBufferPayload
*/
class GeometryPipeline final : public Object {
  OXYGEN_TYPED(GeometryPipeline)
public:
  //! Configuration for the pipeline.
  struct Config {
    size_t queue_capacity = 32;
    uint32_t worker_count = 2;
    bool with_content_hashing = true;
    uint64_t max_data_blob_bytes = data::pak::kDataBlobMaxSize;
  };

  //! Cooked buffer payloads for one mesh LOD.
  struct CookedMeshPayload {
    CookedBufferPayload vertex_buffer;
    CookedBufferPayload index_buffer;
    std::vector<CookedBufferPayload> auxiliary_buffers;
    Bounds3 bounds;
  };

  //! Cooked geometry payload returned by the pipeline.
  struct CookedGeometryPayload {
    data::AssetKey geometry_key;
    std::string virtual_path;
    std::string descriptor_relpath;
    std::vector<std::byte> descriptor_bytes;
    std::vector<CookedMeshPayload> lods;
  };

  //! Buffer bindings used to finalize geometry descriptors.
  struct MeshBufferBindings {
    data::pak::ResourceIndexT vertex_buffer = 0;
    data::pak::ResourceIndexT index_buffer = 0;
    data::pak::ResourceIndexT joint_index_buffer = 0;
    data::pak::ResourceIndexT joint_weight_buffer = 0;
    data::pak::ResourceIndexT inverse_bind_buffer = 0;
    data::pak::ResourceIndexT joint_remap_buffer = 0;
  };

  //! Work submission item.
  struct WorkItem {
    std::string source_id;
    std::string mesh_name;
    std::string storage_mesh_name;
    const void* source_key = nullptr;

    std::vector<MeshLod> lods;

    std::vector<data::AssetKey> material_keys;
    data::AssetKey default_material_key;
    bool want_textures = false;
    bool has_material_textures = false;

    ImportRequest request;
    std::stop_token stop_token;
  };

  //! Work completion result.
  struct WorkResult {
    std::string source_id;
    const void* source_key = nullptr;
    std::optional<CookedGeometryPayload> cooked;
    std::vector<ImportDiagnostic> diagnostics;
    bool success = false;
  };

  //! Create a geometry pipeline using the given ThreadPool.
  OXGN_CNTT_API explicit GeometryPipeline(
    co::ThreadPool& thread_pool, Config config = {});

  OXGN_CNTT_API ~GeometryPipeline();

  OXYGEN_MAKE_NON_COPYABLE(GeometryPipeline)
  OXYGEN_MAKE_NON_MOVABLE(GeometryPipeline)

  //! Start worker coroutines in the given nursery.
  OXGN_CNTT_API auto Start(co::Nursery& nursery) -> void;

  //! Submit work (may suspend if the queue is full).
  OXGN_CNTT_NDAPI [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;

  //! Try to submit work without blocking.
  OXGN_CNTT_NDAPI auto TrySubmit(WorkItem item) -> bool;

  //! Collect one completed result (suspends until ready or closed).
  OXGN_CNTT_NDAPI [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  //! Patch buffer indices and compute descriptor content hash.
  OXGN_CNTT_NDAPI [[nodiscard]] auto FinalizeDescriptorBytes(
    std::span<const MeshBufferBindings> bindings,
    std::span<const std::byte> descriptor_bytes,
    std::vector<ImportDiagnostic>& diagnostics)
    -> co::Co<std::optional<std::vector<std::byte>>>;

  //! Close the input queue.
  OXGN_CNTT_API auto Close() -> void;

  //! Whether any submitted work is still pending completion.
  OXGN_CNTT_NDAPI auto HasPending() const noexcept -> bool;

  //! Number of submitted work items not yet collected.
  OXGN_CNTT_NDAPI auto PendingCount() const noexcept -> size_t;

  //! Get pipeline progress counters.
  OXGN_CNTT_NDAPI auto GetProgress() const noexcept -> PipelineProgress;

private:
  [[nodiscard]] auto Worker() -> co::Co<>;
  auto ReportCancelled(WorkItem item) -> co::Co<>;

  co::ThreadPool& thread_pool_;
  Config config_;

  co::Channel<WorkItem> input_channel_;
  co::Channel<WorkResult> output_channel_;

  std::atomic<size_t> pending_ { 0 };
  std::atomic<size_t> submitted_ { 0 };
  std::atomic<size_t> completed_ { 0 };
  std::atomic<size_t> failed_ { 0 };
  bool started_ = false;
};

static_assert(ImportPipeline<GeometryPipeline>);

} // namespace oxygen::content::import
