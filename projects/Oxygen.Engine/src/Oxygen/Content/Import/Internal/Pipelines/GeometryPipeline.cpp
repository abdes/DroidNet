//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

} // namespace

GeometryPipeline::GeometryPipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

GeometryPipeline::~GeometryPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "GeometryPipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto GeometryPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "GeometryPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto GeometryPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto GeometryPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto GeometryPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .cooked = std::nullopt,
      .finalized_descriptor_bytes = {},
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }

  co_return std::move(*maybe_result);
}

auto GeometryPipeline::Close() -> void { input_channel_.Close(); }

auto GeometryPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto GeometryPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto GeometryPipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto GeometryPipeline::FinalizeDescriptorBytes(
  const std::span<const MeshBufferBindings> bindings,
  const std::span<const std::byte> descriptor_bytes,
  const std::span<const MaterialKeyPatch> material_patches,
  std::vector<ImportDiagnostic>& diagnostics)
  -> co::Co<std::optional<std::vector<std::byte>>>
{
  if (descriptor_bytes.empty()) {
    diagnostics.push_back(MakeErrorDiagnostic(
      "mesh.finalize_failed", "Descriptor bytes are empty", "", ""));
    co_return std::nullopt;
  }

  std::vector input_copy(descriptor_bytes.begin(), descriptor_bytes.end());
  serio::MemoryStream input_stream(
    std::span(input_copy.data(), input_copy.size()));
  serio::Reader reader(input_stream);
  const auto pack_reader = reader.ScopedAlignment(1);

  auto read_pod = [&reader](auto& value) -> bool {
    auto bytes = std::as_writable_bytes(std::span { &value, 1 });
    return static_cast<bool>(reader.ReadBlobInto(bytes));
  };

  data::pak::GeometryAssetDesc asset_desc {};
  if (!read_pod(asset_desc)) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Failed to read geometry asset descriptor", "", ""));
    co_return std::nullopt;
  }

  if (bindings.size() != asset_desc.lod_count) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Descriptor LOD count does not match bindings", "", ""));
    co_return std::nullopt;
  }

  serio::MemoryStream output_stream;
  serio::Writer writer(output_stream);
  const auto pack_writer = writer.ScopedAlignment(1);

  asset_desc.header.content_hash = 0;
  if (!writer.WriteBlob(std::as_bytes(
        std::span<const data::pak::GeometryAssetDesc, 1>(&asset_desc, 1)))) {
    diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
      "Failed to write geometry asset descriptor", "", ""));
    co_return std::nullopt;
  }

  for (uint32_t lod_i = 0; lod_i < asset_desc.lod_count; ++lod_i) {
    data::pak::MeshDesc mesh_desc {};
    if (!read_pod(mesh_desc)) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.finalize_failed", "Failed to read mesh descriptor", "", ""));
      co_return std::nullopt;
    }

    const auto& binding = bindings[lod_i];

    if (static_cast<data::MeshType>(mesh_desc.mesh_type)
      == data::MeshType::kSkinned) {
      data::pak::SkinnedMeshInfo skinned_blob {};
      if (!read_pod(skinned_blob)) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to read skinned mesh blob", "", ""));
        co_return std::nullopt;
      }

      mesh_desc.info.skinned.vertex_buffer = binding.vertex_buffer;
      mesh_desc.info.skinned.index_buffer = binding.index_buffer;
      mesh_desc.info.skinned.joint_index_buffer = binding.joint_index_buffer;
      mesh_desc.info.skinned.joint_weight_buffer = binding.joint_weight_buffer;
      mesh_desc.info.skinned.inverse_bind_buffer = binding.inverse_bind_buffer;
      mesh_desc.info.skinned.joint_remap_buffer = binding.joint_remap_buffer;

      skinned_blob.vertex_buffer = binding.vertex_buffer;
      skinned_blob.index_buffer = binding.index_buffer;
      skinned_blob.joint_index_buffer = binding.joint_index_buffer;
      skinned_blob.joint_weight_buffer = binding.joint_weight_buffer;
      skinned_blob.inverse_bind_buffer = binding.inverse_bind_buffer;
      skinned_blob.joint_remap_buffer = binding.joint_remap_buffer;

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::SkinnedMeshInfo, 1>(
              &skinned_blob, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write skinned mesh blob", "", ""));
        co_return std::nullopt;
      }
    } else if (static_cast<data::MeshType>(mesh_desc.mesh_type)
      == data::MeshType::kProcedural) {
      data::pak::ProceduralMeshInfo procedural_info {};
      if (!read_pod(procedural_info)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read procedural mesh blob", "", ""));
        co_return std::nullopt;
      }

      auto blob = reader.ReadBlob(procedural_info.params_size);
      if (!blob) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read procedural mesh params", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::ProceduralMeshInfo, 1>(
              &procedural_info, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write procedural mesh blob", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const std::byte>(blob->data(), blob->size())))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write procedural mesh params", "", ""));
        co_return std::nullopt;
      }
    } else {
      mesh_desc.info.standard.vertex_buffer = binding.vertex_buffer;
      mesh_desc.info.standard.index_buffer = binding.index_buffer;

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshDesc, 1>(&mesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write mesh descriptor", "", ""));
        co_return std::nullopt;
      }
    }

    for (uint32_t sub = 0; sub < mesh_desc.submesh_count; ++sub) {
      data::pak::SubMeshDesc submesh_desc {};
      if (!read_pod(submesh_desc)) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to read submesh descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::SubMeshDesc, 1>(&submesh_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write submesh descriptor", "", ""));
        co_return std::nullopt;
      }
    }

    for (uint32_t view = 0; view < mesh_desc.mesh_view_count; ++view) {
      data::pak::MeshViewDesc view_desc {};
      if (!read_pod(view_desc)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to read mesh view descriptor", "", ""));
        co_return std::nullopt;
      }

      if (!writer.WriteBlob(std::as_bytes(
            std::span<const data::pak::MeshViewDesc, 1>(&view_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write mesh view descriptor", "", ""));
        co_return std::nullopt;
      }
    }
  }

  const auto output_span = output_stream.Data();
  std::vector output_bytes(output_span.begin(), output_span.end());

  if (!material_patches.empty()) {
    serio::MemoryStream patch_stream(
      std::span(output_bytes.data(), output_bytes.size()));
    serio::Writer patch_writer(patch_stream);
    const auto patch_pack = patch_writer.ScopedAlignment(1);

    for (const auto& patch : material_patches) {
      const auto offset = static_cast<size_t>(patch.material_key_offset);
      if (offset + sizeof(data::AssetKey) > output_bytes.size()) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Material patch offset is outside descriptor bounds", "", ""));
        co_return std::nullopt;
      }

      if (!patch_stream.Seek(offset)) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to seek to material patch offset", "", ""));
        co_return std::nullopt;
      }

      if (!patch_writer.WriteBlob(
            std::as_bytes(std::span<const data::AssetKey, 1>(&patch.key, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "mesh.finalize_failed", "Failed to write material patch", "", ""));
        co_return std::nullopt;
      }
    }
  }

  if (config_.with_content_hashing) {
    const auto hash = co_await thread_pool_.Run(
      [bytes = std::span<const std::byte>(output_bytes.data(),
         output_bytes.size())](co::ThreadPool::CancelToken canceled) noexcept {
        DLOG_F(1, "GeometryPipeline: Compute content hash");
        if (canceled) {
          return uint64_t { 0 };
        }
        return util::ComputeContentHash(bytes);
      });

    if (hash != 0) {
      asset_desc.header.content_hash = hash;
      serio::MemoryStream patch_stream(
        std::span(output_bytes.data(), output_bytes.size()));
      serio::Writer patch_writer(patch_stream);
      const auto patch_pack = patch_writer.ScopedAlignment(1);
      if (!patch_writer.WriteBlob(
            std::as_bytes(std::span<const data::pak::GeometryAssetDesc, 1>(
              &asset_desc, 1)))) {
        diagnostics.push_back(MakeErrorDiagnostic("mesh.finalize_failed",
          "Failed to write geometry asset descriptor hash", "", ""));
        co_return std::nullopt;
      }
    }
  }

  co_return output_bytes;
}

auto GeometryPipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    if (item.on_started) {
      item.on_started();
    }

    std::vector<ImportDiagnostic> diagnostics;
    auto finalized = co_await FinalizeDescriptorBytes(item.bindings,
      item.cooked.descriptor_bytes, item.material_patches, diagnostics);

    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const bool success = finalized.has_value() && diagnostics.empty();
    WorkResult result {
      .source_id = std::move(item.source_id),
      .cooked = success
        ? std::optional<MeshBuildPipeline::CookedGeometryPayload>(
            std::move(item.cooked))
        : std::nullopt,
      .finalized_descriptor_bytes = finalized.has_value()
        ? std::move(*finalized)
        : std::vector<std::byte> {},
      .diagnostics = std::move(diagnostics),
      .success = success,
    };

    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto GeometryPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult canceled {
    .source_id = std::move(item.source_id),
    .cooked = std::nullopt,
    .finalized_descriptor_bytes = {},
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
