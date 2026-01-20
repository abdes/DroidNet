//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/WorkDispatcher.h>

#include <limits>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>

namespace oxygen::content::import::detail {

WorkDispatcher::WorkDispatcher(ImportSession& session,
  oxygen::observer_ptr<co::ThreadPool> thread_pool,
  const ImportConcurrency& concurrency, std::stop_token stop_token)
  : session_(session)
  , thread_pool_(thread_pool)
  , concurrency_(concurrency)
  , stop_token_(std::move(stop_token))
{
}

auto WorkDispatcher::DefaultMaterialKey() -> data::AssetKey
{
  return data::MaterialAsset::CreateDefault()->GetAssetKey();
}

auto WorkDispatcher::MakeErrorDiagnostic(std::string code, std::string message,
  std::string_view source_id, std::string_view object_path) -> ImportDiagnostic
{
  return ImportDiagnostic {
    .severity = ImportSeverity::kError,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = std::string(source_id),
    .object_path = std::string(object_path),
  };
}

auto WorkDispatcher::MakeWarningDiagnostic(std::string code,
  std::string message, std::string_view source_id, std::string_view object_path)
  -> ImportDiagnostic
{
  return ImportDiagnostic {
    .severity = ImportSeverity::kWarning,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = std::string(source_id),
    .object_path = std::string(object_path),
  };
}

auto WorkDispatcher::AddDiagnostics(
  ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
{
  for (auto& diagnostic : diagnostics) {
    session.AddDiagnostic(std::move(diagnostic));
  }
}

auto WorkDispatcher::EmitGeometryPayload(GeometryPipeline& pipeline,
  GeometryPipeline::WorkResult result) -> co::Co<bool>
{
  if (!result.success || !result.cooked.has_value()) {
    AddDiagnostics(session_, std::move(result.diagnostics));
    co_return false;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& cooked = *result.cooked;
  auto& buffer_emitter = session_.BufferEmitter();
  auto& asset_emitter = session_.AssetEmitter();

  std::vector<GeometryPipeline::MeshBufferBindings> bindings;
  bindings.reserve(cooked.lods.size());

  bool ok = true;
  for (auto& lod : cooked.lods) {
    GeometryPipeline::MeshBufferBindings binding {};
    binding.vertex_buffer = buffer_emitter.Emit(std::move(lod.vertex_buffer));
    binding.index_buffer = buffer_emitter.Emit(std::move(lod.index_buffer));

    if (lod.auxiliary_buffers.size() == 4u) {
      binding.joint_index_buffer
        = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[0]));
      binding.joint_weight_buffer
        = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[1]));
      binding.inverse_bind_buffer
        = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[2]));
      binding.joint_remap_buffer
        = buffer_emitter.Emit(std::move(lod.auxiliary_buffers[3]));
    } else if (!lod.auxiliary_buffers.empty()) {
      session_.AddDiagnostic(MakeErrorDiagnostic("mesh.aux_buffer_count",
        "Unexpected auxiliary buffer count for mesh LOD", result.source_id,
        ""));
      ok = false;
    }

    bindings.push_back(binding);
  }

  std::vector<ImportDiagnostic> finalize_diagnostics;
  const auto finalized = co_await pipeline.FinalizeDescriptorBytes(
    bindings, cooked.descriptor_bytes, finalize_diagnostics);
  AddDiagnostics(session_, std::move(finalize_diagnostics));

  if (!finalized.has_value()) {
    co_return false;
  }

  asset_emitter.Emit(cooked.geometry_key, data::AssetType::kGeometry,
    cooked.virtual_path, cooked.descriptor_relpath, *finalized);
  co_return ok;
}

auto WorkDispatcher::EmitTexturePayload(TexturePipeline::WorkResult& result)
  -> std::optional<uint32_t>
{
  if (result.used_placeholder) {
    const bool has_diagnostics = !result.diagnostics.empty();
    if (has_diagnostics) {
      for (auto& diagnostic : result.diagnostics) {
        if (diagnostic.severity == ImportSeverity::kError) {
          diagnostic.severity = ImportSeverity::kWarning;
        }
      }
    }
    AddDiagnostics(session_, std::move(result.diagnostics));
    session_.AddDiagnostic(MakeWarningDiagnostic("texture.placeholder_used",
      "Texture cooking failed; using fallback texture", result.source_id, ""));

    (void)session_.TextureEmitter();
    return data::pak::kFallbackResourceIndex;
  }

  if (!result.success || !result.cooked.has_value()) {
    const bool has_diagnostics = !result.diagnostics.empty();
    if (has_diagnostics) {
      for (auto& diagnostic : result.diagnostics) {
        if (diagnostic.severity == ImportSeverity::kError) {
          diagnostic.severity = ImportSeverity::kWarning;
        }
      }
    }
    AddDiagnostics(session_, std::move(result.diagnostics));
    if (!has_diagnostics) {
      return std::nullopt;
    }
    using data::pak::ResourceIndexT;
    constexpr ResourceIndexT kErrorTextureIndex
      = std::numeric_limits<ResourceIndexT>::max();
    return kErrorTextureIndex;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& emitter = session_.TextureEmitter();
  return emitter.Emit(std::move(*result.cooked));
}

auto WorkDispatcher::EmitBufferPayload(BufferPipeline::WorkResult result)
  -> bool
{
  if (!result.success) {
    AddDiagnostics(session_, std::move(result.diagnostics));
    return false;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& emitter = session_.BufferEmitter();
  (void)emitter.Emit(std::move(result.cooked));
  return true;
}

auto WorkDispatcher::EmitMaterialPayload(MaterialPipeline::WorkResult result)
  -> bool
{
  if (!result.success || !result.cooked.has_value()) {
    AddDiagnostics(session_, std::move(result.diagnostics));
    return false;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& cooked = *result.cooked;
  auto& emitter = session_.AssetEmitter();
  emitter.Emit(cooked.material_key, data::AssetType::kMaterial,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes);
  return true;
}

auto WorkDispatcher::EmitScenePayload(ScenePipeline::WorkResult result) -> bool
{
  if (!result.success || !result.cooked.has_value()) {
    AddDiagnostics(session_, std::move(result.diagnostics));
    return false;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& cooked = *result.cooked;
  auto& emitter = session_.AssetEmitter();
  emitter.Emit(cooked.scene_key, data::AssetType::kScene, cooked.virtual_path,
    cooked.descriptor_relpath, cooked.descriptor_bytes);
  return true;
}

auto WorkDispatcher::UpdateMaterialBindings(
  const std::unordered_map<std::string, uint32_t>& texture_indices,
  MaterialPipeline::WorkItem& item, std::vector<ImportDiagnostic>& diagnostics)
  -> void
{
  auto resolve_binding = [&](MaterialTextureBinding& binding,
                           std::string_view label) {
    if (!binding.assigned || binding.source_id.empty()) {
      return;
    }

    const auto it = texture_indices.find(binding.source_id);
    if (it == texture_indices.end()) {
      diagnostics.push_back(MakeWarningDiagnostic("material.texture_missing",
        "Material texture dependency is missing", item.source_id,
        binding.source_id));
      DLOG_F(WARNING, "Material '{}' missing texture '{}' ({})", item.source_id,
        binding.source_id, label);
      using data::pak::ResourceIndexT;
      constexpr ResourceIndexT kErrorTextureIndex
        = std::numeric_limits<ResourceIndexT>::max();
      binding.index = kErrorTextureIndex;
      binding.assigned = true;
      return;
    }

    binding.index = it->second;
    DLOG_F(1, "Material '{}' bind {} -> '{}' index={}", item.source_id, label,
      binding.source_id, binding.index);
  };

  resolve_binding(item.textures.base_color, "base_color");
  resolve_binding(item.textures.normal, "normal");
  resolve_binding(item.textures.metallic, "metallic");
  resolve_binding(item.textures.roughness, "roughness");
  resolve_binding(item.textures.ambient_occlusion, "occlusion");
  resolve_binding(item.textures.emissive, "emissive");
  resolve_binding(item.textures.specular, "specular");
  resolve_binding(item.textures.sheen_color, "sheen_color");
  resolve_binding(item.textures.clearcoat, "clearcoat");
  resolve_binding(item.textures.clearcoat_normal, "clearcoat_normal");
  resolve_binding(item.textures.transmission, "transmission");
  resolve_binding(item.textures.thickness, "thickness");
}

auto WorkDispatcher::EnsureTexturePipeline(co::Nursery& nursery)
  -> TexturePipeline&
{
  if (!texture_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    texture_pipeline_ = std::make_unique<TexturePipeline>(*thread_pool_,
      TexturePipeline::Config {
        .queue_capacity = concurrency_.texture.queue_capacity,
        .worker_count = concurrency_.texture.workers,
        .with_content_hashing = with_content_hashing,
      });
    texture_pipeline_->Start(nursery);
  }
  return *texture_pipeline_;
}

auto WorkDispatcher::EnsureBufferPipeline(co::Nursery& nursery)
  -> BufferPipeline&
{
  if (!buffer_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    buffer_pipeline_ = std::make_unique<BufferPipeline>(*thread_pool_,
      BufferPipeline::Config {
        .queue_capacity = concurrency_.buffer.queue_capacity,
        .worker_count = concurrency_.buffer.workers,
        .with_content_hashing = with_content_hashing,
      });
    buffer_pipeline_->Start(nursery);
  }
  return *buffer_pipeline_;
}

auto WorkDispatcher::EnsureMaterialPipeline(co::Nursery& nursery)
  -> MaterialPipeline&
{
  if (!material_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    material_pipeline_ = std::make_unique<MaterialPipeline>(*thread_pool_,
      MaterialPipeline::Config {
        .queue_capacity = concurrency_.material.queue_capacity,
        .worker_count = concurrency_.material.workers,
        .with_content_hashing = with_content_hashing,
      });
    material_pipeline_->Start(nursery);
  }
  return *material_pipeline_;
}

auto WorkDispatcher::EnsureGeometryPipeline(co::Nursery& nursery)
  -> GeometryPipeline&
{
  if (!geometry_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    geometry_pipeline_ = std::make_unique<GeometryPipeline>(*thread_pool_,
      GeometryPipeline::Config {
        .queue_capacity = concurrency_.geometry.queue_capacity,
        .worker_count = concurrency_.geometry.workers,
        .with_content_hashing = with_content_hashing,
      });
    geometry_pipeline_->Start(nursery);
  }
  return *geometry_pipeline_;
}

auto WorkDispatcher::EnsureScenePipeline(co::Nursery& nursery) -> ScenePipeline&
{
  if (!scene_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    scene_pipeline_ = std::make_unique<ScenePipeline>(*thread_pool_,
      ScenePipeline::Config {
        .queue_capacity = concurrency_.scene.queue_capacity,
        .worker_count = concurrency_.scene.workers,
        .with_content_hashing = with_content_hashing,
      });
    scene_pipeline_->Start(nursery);
  }
  return *scene_pipeline_;
}

auto WorkDispatcher::ClosePipelines() noexcept -> void
{
  if (texture_pipeline_) {
    texture_pipeline_->Close();
  }
  if (buffer_pipeline_) {
    buffer_pipeline_->Close();
  }
  if (material_pipeline_) {
    material_pipeline_->Close();
  }
  if (geometry_pipeline_) {
    geometry_pipeline_->Close();
  }
  if (scene_pipeline_) {
    scene_pipeline_->Close();
  }
}

auto WorkDispatcher::Run(PlanContext context, co::Nursery& nursery)
  -> co::Co<bool>
{
  std::unordered_map<std::string, uint32_t> texture_indices;
  std::unordered_map<PlanItemId, data::AssetKey> material_keys;
  std::unordered_map<PlanItemId, data::AssetKey> geometry_keys;
  std::unordered_map<std::string, PlanItemId> texture_item_ids;
  std::unordered_map<std::string, PlanItemId> texture_item_ids_by_source;
  std::unordered_map<const void*, PlanItemId> texture_item_ids_by_key;
  std::unordered_map<std::string, PlanItemId> buffer_item_ids_by_source;
  std::unordered_map<std::string, PlanItemId> material_item_ids_by_source;
  std::unordered_map<std::string, PlanItemId> geometry_item_ids_by_source;
  std::unordered_map<const void*, PlanItemId> geometry_item_ids_by_key;
  std::unordered_map<std::string, PlanItemId> scene_item_ids_by_source;
  size_t pending_textures = 0;
  size_t pending_buffers = 0;
  size_t pending_materials = 0;
  size_t pending_geometries = 0;
  size_t pending_scenes = 0;

  co::detail::ScopeGuard close_guard([&]() noexcept { ClosePipelines(); });

  auto resolve_texture_item =
    [&](
      const TexturePipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (!result.texture_id.empty()) {
      const auto it = texture_item_ids.find(result.texture_id);
      if (it != texture_item_ids.end()) {
        const auto item_id = it->second;
        texture_item_ids.erase(it);
        return item_id;
      }
    }

    if (result.source_key != nullptr) {
      const auto it = texture_item_ids_by_key.find(result.source_key);
      if (it != texture_item_ids_by_key.end()) {
        const auto item_id = it->second;
        texture_item_ids_by_key.erase(it);
        return item_id;
      }
    }

    if (!result.source_id.empty()) {
      const auto it = texture_item_ids_by_source.find(result.source_id);
      if (it != texture_item_ids_by_source.end()) {
        const auto item_id = it->second;
        texture_item_ids_by_source.erase(it);
        return item_id;
      }
    }

    return std::nullopt;
  };

  const auto item_count = context.steps.size();
  std::vector<uint8_t> submitted(item_count, 0U);
  std::vector<uint8_t> completed(item_count, 0U);
  size_t completed_count = 0U;

  std::vector<std::vector<PlanItemId>> dependents(item_count);
  for (const auto& step : context.steps) {
    for (const auto prerequisite_id : step.prerequisites) {
      const auto u_prereq = prerequisite_id.get();
      dependents[u_prereq].push_back(step.item_id);
    }
  }

  std::deque<PlanItemId> ready_queue;
  for (const auto& step : context.steps) {
    auto& tracker = context.planner.Tracker(step.item_id);
    if (tracker.IsReady()) {
      ready_queue.push_back(step.item_id);
    }
  }

  auto enqueue_ready = [&](const PlanItemId item_id) {
    const auto u_item = item_id.get();
    if (submitted[u_item] != 0U) {
      return;
    }
    ready_queue.push_back(item_id);
  };

  auto mark_complete = [&](const PlanItemId item_id) {
    const auto u_item = item_id.get();
    if (completed[u_item] != 0U) {
      return;
    }
    completed[u_item] = 1U;
    ++completed_count;
    for (const auto dependent : dependents[u_item]) {
      auto& tracker = context.planner.Tracker(dependent);
      if (tracker.MarkReady({ item_id })) {
        enqueue_ready(dependent);
      }
    }
  };

  auto resolve_buffer_item =
    [&](const BufferPipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (!result.source_id.empty()) {
      const auto it = buffer_item_ids_by_source.find(result.source_id);
      if (it != buffer_item_ids_by_source.end()) {
        const auto item_id = it->second;
        buffer_item_ids_by_source.erase(it);
        return item_id;
      }
    }
    return std::nullopt;
  };

  auto resolve_material_item =
    [&](
      const MaterialPipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (!result.source_id.empty()) {
      const auto it = material_item_ids_by_source.find(result.source_id);
      if (it != material_item_ids_by_source.end()) {
        const auto item_id = it->second;
        material_item_ids_by_source.erase(it);
        return item_id;
      }
    }
    return std::nullopt;
  };

  auto resolve_geometry_item =
    [&](
      const GeometryPipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (result.source_key != nullptr) {
      const auto it = geometry_item_ids_by_key.find(result.source_key);
      if (it != geometry_item_ids_by_key.end()) {
        const auto item_id = it->second;
        geometry_item_ids_by_key.erase(it);
        return item_id;
      }
    }

    if (!result.source_id.empty()) {
      const auto it = geometry_item_ids_by_source.find(result.source_id);
      if (it != geometry_item_ids_by_source.end()) {
        const auto item_id = it->second;
        geometry_item_ids_by_source.erase(it);
        return item_id;
      }
    }

    return std::nullopt;
  };

  auto resolve_scene_item =
    [&](const ScenePipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (!result.source_id.empty()) {
      const auto it = scene_item_ids_by_source.find(result.source_id);
      if (it != scene_item_ids_by_source.end()) {
        const auto item_id = it->second;
        scene_item_ids_by_source.erase(it);
        return item_id;
      }
    }
    return std::nullopt;
  };

  auto process_texture_result
    = [&](TexturePipeline::WorkResult result) -> co::Co<bool> {
    auto index = EmitTexturePayload(result);
    if (!index.has_value()) {
      co_return false;
    }

    if (!result.source_id.empty()) {
      texture_indices.insert_or_assign(result.source_id, *index);
    }

    if (const auto item_id = resolve_texture_item(result)) {
      mark_complete(*item_id);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.texture_unmapped",
      "Texture result could not be mapped to a plan item", result.source_id,
      ""));
    co_return false;
  };

  auto process_buffer_result
    = [&](BufferPipeline::WorkResult result) -> co::Co<bool> {
    if (!EmitBufferPayload(result)) {
      co_return false;
    }

    if (const auto item_id = resolve_buffer_item(result)) {
      mark_complete(*item_id);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.buffer_unmapped",
      "Buffer result could not be mapped to a plan item", "", ""));
    co_return false;
  };

  auto process_material_result
    = [&](MaterialPipeline::WorkResult result) -> co::Co<bool> {
    if (!EmitMaterialPayload(result)) {
      co_return false;
    }

    if (const auto item_id = resolve_material_item(result)) {
      if (result.cooked.has_value()) {
        material_keys.emplace(*item_id, result.cooked->material_key);
      }
      mark_complete(*item_id);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.material_unmapped",
      "Material result could not be mapped to a plan item", result.source_id,
      ""));
    co_return false;
  };

  auto process_geometry_result
    = [&](GeometryPipeline::WorkResult result) -> co::Co<bool> {
    const auto item_id = resolve_geometry_item(result);
    std::optional<data::AssetKey> geometry_key;
    if (result.cooked.has_value()) {
      geometry_key = result.cooked->geometry_key;
    }

    if (!geometry_pipeline_) {
      co_return false;
    }

    if (!co_await EmitGeometryPayload(*geometry_pipeline_, std::move(result))) {
      co_return false;
    }

    if (item_id.has_value()) {
      if (geometry_key.has_value()) {
        geometry_keys.emplace(*item_id, *geometry_key);
      }
      mark_complete(*item_id);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.geometry_unmapped",
      "Geometry result could not be mapped to a plan item", "", ""));
    co_return false;
  };

  auto process_scene_result
    = [&](ScenePipeline::WorkResult result) -> co::Co<bool> {
    const auto item_id = resolve_scene_item(result);
    if (!EmitScenePayload(std::move(result))) {
      co_return false;
    }

    if (item_id.has_value()) {
      mark_complete(*item_id);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.scene_unmapped",
      "Scene result could not be mapped to a plan item", "", ""));
    co_return false;
  };

  auto collect_one_result = [&]() -> co::Co<bool> {
    if (pending_textures > 0 && texture_pipeline_) {
      auto result = co_await texture_pipeline_->Collect();
      --pending_textures;
      co_return co_await process_texture_result(std::move(result));
    }

    if (pending_buffers > 0 && buffer_pipeline_) {
      auto result = co_await buffer_pipeline_->Collect();
      --pending_buffers;
      co_return co_await process_buffer_result(std::move(result));
    }

    if (pending_materials > 0 && material_pipeline_) {
      auto result = co_await material_pipeline_->Collect();
      --pending_materials;
      co_return co_await process_material_result(std::move(result));
    }

    if (pending_geometries > 0 && geometry_pipeline_) {
      auto result = co_await geometry_pipeline_->Collect();
      --pending_geometries;
      co_return co_await process_geometry_result(std::move(result));
    }

    if (pending_scenes > 0 && scene_pipeline_) {
      auto result = co_await scene_pipeline_->Collect();
      --pending_scenes;
      co_return co_await process_scene_result(std::move(result));
    }

    co_return false;
  };

  auto submit_texture = [&](const PlanItemId item_id) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Texture(item.work_handle);
    if (!payload.item.texture_id.empty()) {
      texture_item_ids.insert_or_assign(payload.item.texture_id, item_id);
    }
    if (!payload.item.source_id.empty()) {
      texture_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }
    if (payload.item.source_key != nullptr) {
      texture_item_ids_by_key.insert_or_assign(
        payload.item.source_key, item_id);
    }

    auto& pipeline = EnsureTexturePipeline(nursery);
    while (pending_textures >= concurrency_.texture.queue_capacity) {
      if (!co_await collect_one_result()) {
        co_return false;
      }
    }
    ++pending_textures;
    co_await pipeline.Submit(std::move(payload.item));
    co_return true;
  };

  auto submit_buffer = [&](const PlanItemId item_id) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Buffer(item.work_handle);
    if (!payload.item.source_id.empty()) {
      buffer_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }

    auto& pipeline = EnsureBufferPipeline(nursery);
    while (pending_buffers >= concurrency_.buffer.queue_capacity) {
      if (!co_await collect_one_result()) {
        co_return false;
      }
    }
    ++pending_buffers;
    co_await pipeline.Submit(std::move(payload.item));
    co_return true;
  };

  auto submit_material = [&](const PlanItemId item_id) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Material(item.work_handle);
    std::vector<ImportDiagnostic> resolve_diags;
    UpdateMaterialBindings(texture_indices, payload.item, resolve_diags);
    AddDiagnostics(session_, std::move(resolve_diags));

    if (!payload.item.source_id.empty()) {
      material_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }

    auto& pipeline = EnsureMaterialPipeline(nursery);
    while (pending_materials >= concurrency_.material.queue_capacity) {
      if (!co_await collect_one_result()) {
        co_return false;
      }
    }
    ++pending_materials;
    co_await pipeline.Submit(std::move(payload.item));
    co_return true;
  };

  auto submit_geometry = [&](const PlanItemId item_id) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Geometry(item.work_handle);
    payload.item.material_keys.clear();
    payload.item.material_keys.reserve(context.material_slots.size());
    for (const auto material_item : context.material_slots) {
      const auto it = material_keys.find(material_item);
      if (it != material_keys.end()) {
        payload.item.material_keys.push_back(it->second);
      } else {
        payload.item.material_keys.push_back(DefaultMaterialKey());
      }
    }

    if (!payload.item.source_id.empty()) {
      geometry_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }
    if (payload.item.source_key != nullptr) {
      geometry_item_ids_by_key.insert_or_assign(
        payload.item.source_key, item_id);
    }

    auto& pipeline = EnsureGeometryPipeline(nursery);
    while (pending_geometries >= concurrency_.geometry.queue_capacity) {
      if (!co_await collect_one_result()) {
        co_return false;
      }
    }
    ++pending_geometries;
    co_await pipeline.Submit(std::move(payload.item));
    co_return true;
  };

  auto submit_scene = [&](const PlanItemId item_id) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Scene(item.work_handle);
    payload.item.geometry_keys.clear();
    payload.item.geometry_keys.reserve(context.geometry_items.size());
    for (const auto geometry_item : context.geometry_items) {
      const auto it = geometry_keys.find(geometry_item);
      if (it != geometry_keys.end()) {
        payload.item.geometry_keys.push_back(it->second);
      } else {
        session_.AddDiagnostic(MakeErrorDiagnostic("scene.geometry_key_missing",
          "Missing geometry key for scene dependency", item.debug_name, ""));
      }
    }

    if (!payload.item.source_id.empty()) {
      scene_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }

    auto& pipeline = EnsureScenePipeline(nursery);
    while (pending_scenes >= concurrency_.scene.queue_capacity) {
      if (!co_await collect_one_result()) {
        co_return false;
      }
    }
    ++pending_scenes;
    co_await pipeline.Submit(std::move(payload.item));
    co_return true;
  };

  auto submit_item = [&](const PlanItemId item_id) -> co::Co<bool> {
    const auto u_item = item_id.get();
    if (submitted[u_item] != 0U) {
      co_return true;
    }
    submitted[u_item] = 1U;

    const auto& item = context.planner.Item(item_id);
    switch (item.kind) {
    case PlanItemKind::kTextureResource:
      co_return co_await submit_texture(item_id);
    case PlanItemKind::kBufferResource:
      co_return co_await submit_buffer(item_id);
    case PlanItemKind::kMaterialAsset:
      co_return co_await submit_material(item_id);
    case PlanItemKind::kGeometryAsset:
      co_return co_await submit_geometry(item_id);
    case PlanItemKind::kSceneAsset:
      co_return co_await submit_scene(item_id);
    case PlanItemKind::kAudioResource:
      session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.unhandled_kind",
        "Unhandled plan item kind in import", item.debug_name, ""));
      co_return false;
    }

    co_return false;
  };

  while (completed_count < item_count) {
    while (!ready_queue.empty()) {
      const auto item_id = ready_queue.front();
      ready_queue.pop_front();
      if (!co_await submit_item(item_id)) {
        co_return false;
      }
    }

    if (stop_token_.stop_requested()) {
      co_return false;
    }

    const auto pending_total = pending_textures + pending_buffers
      + pending_materials + pending_geometries + pending_scenes;
    if (pending_total == 0U) {
      session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.deadlock",
        "Import plan has no pending work but is not complete", "", ""));
      co_return false;
    }

    if (!co_await collect_one_result()) {
      co_return false;
    }
  }

  co_return true;
}

} // namespace oxygen::content::import::detail
