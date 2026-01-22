//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/WorkDispatcher.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <variant>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Channel.h>
#include <Oxygen/OxCo/Detail/ScopeGuard.h>
#include <Oxygen/OxCo/Event.h>

namespace oxygen::content::import::detail {

namespace {

  using KindAvailability = std::array<bool, kPlanKindCount>;

  //! Strategy interface for selecting the next ready item to submit.
  class SubmissionStrategy {
  public:
    virtual ~SubmissionStrategy() = default;

    virtual auto AddReady(PlanItemId item_id, PlanItemKind kind) -> void = 0;

    [[nodiscard]] virtual auto HasReady() const -> bool = 0;

    [[nodiscard]] virtual auto NextReady(const KindAvailability& availability)
      -> std::optional<PlanItemId>
      = 0;
  };

  //! Round-robin submission strategy that skips full kinds.
  class RoundRobinSubmissionStrategy final : public SubmissionStrategy {
  public:
    auto AddReady(PlanItemId item_id, PlanItemKind kind) -> void override
    {
      buckets_[static_cast<size_t>(kind)].push_back(item_id);
    }

    [[nodiscard]] auto HasReady() const -> bool override
    {
      for (const auto& bucket : buckets_) {
        if (!bucket.empty()) {
          return true;
        }
      }
      return false;
    }

    [[nodiscard]] auto NextReady(const KindAvailability& availability)
      -> std::optional<PlanItemId> override
    {
      if (!HasReady()) {
        return std::nullopt;
      }

      for (size_t attempt = 0U; attempt < buckets_.size(); ++attempt) {
        const auto kind_index = (cursor_ + attempt) % buckets_.size();
        if (buckets_[kind_index].empty()) {
          continue;
        }
        if (!availability[kind_index]) {
          continue;
        }

        auto item_id = buckets_[kind_index].front();
        buckets_[kind_index].pop_front();
        cursor_ = (kind_index + 1U) % buckets_.size();
        return item_id;
      }

      return std::nullopt;
    }

  private:
    std::array<std::deque<PlanItemId>, kPlanKindCount> buckets_ {};
    size_t cursor_ = 0U;
  };

} // namespace

WorkDispatcher::WorkDispatcher(ImportSession& session,
  observer_ptr<co::ThreadPool> thread_pool,
  const ImportConcurrency& concurrency, std::stop_token stop_token,
  std::optional<ProgressReporter> progress)
  : session_(session)
  , thread_pool_(thread_pool)
  , concurrency_(concurrency)
  , stop_token_(std::move(stop_token))
  , progress_(std::move(progress))
{
}

auto WorkDispatcher::ProgressReporter::ReportItemProgress(
  ProgressEventKind kind, ImportPhase phase, float overall_progress,
  std::string message, std::string item_kind, std::string item_name) const
  -> void
{
  if (!on_progress) {
    return;
  }

  DCHECK_F(kind == ProgressEventKind::kItemStarted
      || kind == ProgressEventKind::kItemFinished,
    "ReportItemProgress expects item start or finish kind");
  auto progress = kind == ProgressEventKind::kItemStarted
    ? MakeItemStarted(job_id, phase, overall_progress, std::move(item_kind),
        std::move(item_name), std::move(message))
    : MakeItemFinished(job_id, phase, overall_progress, std::move(item_kind),
        std::move(item_name), std::move(message));
  on_progress(progress);
}

auto WorkDispatcher::ProgressReporter::ReportItemCollected(ImportPhase phase,
  float overall_progress, std::string message, std::string item_kind,
  float queue_load) const -> void
{
  if (!on_progress) {
    return;
  }

  auto progress = MakeItemCollected(job_id, phase, overall_progress,
    std::move(item_kind), queue_load, std::move(message));
  on_progress(progress);
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

auto WorkDispatcher::EmitGeometryPayload(
  const MeshBuildPipeline::CookedGeometryPayload& cooked,
  const std::span<const std::byte> finalized_descriptor_bytes) -> bool
{
  auto& asset_emitter = session_.AssetEmitter();

  asset_emitter.Emit(cooked.geometry_key, data::AssetType::kGeometry,
    cooked.virtual_path, cooked.descriptor_relpath,
    std::vector<std::byte>(
      finalized_descriptor_bytes.begin(), finalized_descriptor_bytes.end()));
  return true;
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
  -> std::optional<uint32_t>
{
  if (!result.success) {
    AddDiagnostics(session_, std::move(result.diagnostics));
    return std::nullopt;
  }

  AddDiagnostics(session_, std::move(result.diagnostics));

  auto& emitter = session_.BufferEmitter();
  return emitter.Emit(std::move(result.cooked));
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

auto WorkDispatcher::EnsureMeshBuildPipeline(co::Nursery& nursery)
  -> MeshBuildPipeline&
{
  if (!mesh_build_pipeline_) {
    const bool with_content_hashing
      = session_.Request().options.with_content_hashing;
    mesh_build_pipeline_ = std::make_unique<MeshBuildPipeline>(*thread_pool_,
      MeshBuildPipeline::Config {
        .queue_capacity = concurrency_.mesh_build.queue_capacity,
        .worker_count = concurrency_.mesh_build.workers,
        .with_content_hashing = with_content_hashing,
      });
    mesh_build_pipeline_->Start(nursery);
  }
  return *mesh_build_pipeline_;
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
  if (mesh_build_pipeline_) {
    mesh_build_pipeline_->Close();
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
  auto phase_for_kind
    = [](const PlanItemKind) -> ImportPhase { return ImportPhase::kWorking; };

  auto kind_label = [](const PlanItemKind kind) -> std::string {
    return std::string(to_string(kind));
  };

  std::unordered_map<std::string, uint32_t> texture_indices;
  std::unordered_map<PlanItemId, data::AssetKey> material_keys;
  std::unordered_map<PlanItemId, data::AssetKey> geometry_keys;
  std::unordered_map<std::string, PlanItemId> texture_item_ids;
  std::unordered_map<std::string, PlanItemId> texture_item_ids_by_source;
  std::unordered_map<const void*, PlanItemId> texture_item_ids_by_key;
  std::unordered_map<std::string, PlanItemId> buffer_item_ids_by_source;
  std::unordered_map<std::string, PlanItemId> material_item_ids_by_source;
  std::unordered_map<std::string, PlanItemId> mesh_build_item_ids_by_source;
  std::unordered_map<const void*, PlanItemId> mesh_build_item_ids_by_key;
  struct MeshBuildReady final {
    MeshBuildPipeline::WorkResult result;
    std::vector<MeshBufferBindings> bindings;
  };

  std::unordered_map<PlanItemId, MeshBuildReady> mesh_build_results;
  std::unordered_map<std::string, PlanItemId> geometry_item_ids_by_source;
  std::unordered_map<std::string, PlanItemId> scene_item_ids_by_source;
  std::atomic<size_t> pending_textures { 0U };
  std::atomic<size_t> pending_buffers { 0U };
  std::atomic<size_t> pending_materials { 0U };
  std::atomic<size_t> pending_mesh_builds { 0U };
  std::atomic<size_t> pending_geometries { 0U };
  std::atomic<size_t> pending_scenes { 0U };
  std::atomic<size_t> pending_envelopes { 0U };

  auto scheduler = std::make_unique<RoundRobinSubmissionStrategy>();

  enum class ResultKind : uint8_t {
    kTexture,
    kBuffer,
    kMaterial,
    kMeshBuild,
    kGeometry,
    kScene,
  };

  using ResultPayload
    = std::variant<TexturePipeline::WorkResult, BufferPipeline::WorkResult,
      MaterialPipeline::WorkResult, MeshBuildPipeline::WorkResult,
      GeometryPipeline::WorkResult, ScenePipeline::WorkResult>;

  struct ResultEnvelope {
    ResultKind kind;
    ResultPayload payload;
  };

  const auto item_count = context.steps.size();
  const auto result_capacity = (std::max)(size_t { 1 }, item_count);
  co::Channel<ResultEnvelope> result_channel(result_capacity);
  co::Channel<uint8_t> collector_kick(1U);

  auto PendingTotal = [&]() -> size_t {
    return pending_textures.load(std::memory_order_acquire)
      + pending_buffers.load(std::memory_order_acquire)
      + pending_materials.load(std::memory_order_acquire)
      + pending_mesh_builds.load(std::memory_order_acquire)
      + pending_geometries.load(std::memory_order_acquire)
      + pending_scenes.load(std::memory_order_acquire);
  };

  auto NotifyCollector
    = [&]() -> void { (void)collector_kick.TrySend(uint8_t { 1 }); };

  co::detail::ScopeGuard close_guard([&]() noexcept { ClosePipelines(); });

  auto resolve_texture_item =
    [&](
      const TexturePipeline::WorkResult& result) -> std::optional<PlanItemId> {
    if (!result.texture_id.empty()) {
      const auto it = texture_item_ids.find(result.texture_id);
      if (it != texture_item_ids.end()) {
        const auto item_id = it->second;
        texture_item_ids.erase(it);
        if (result.source_key != nullptr) {
          texture_item_ids_by_key.erase(result.source_key);
        }
        if (!result.source_id.empty()) {
          texture_item_ids_by_source.erase(result.source_id);
        }
        return item_id;
      }
    }

    if (result.source_key != nullptr) {
      const auto it = texture_item_ids_by_key.find(result.source_key);
      if (it != texture_item_ids_by_key.end()) {
        const auto item_id = it->second;
        texture_item_ids_by_key.erase(it);
        if (!result.texture_id.empty()) {
          texture_item_ids.erase(result.texture_id);
        }
        if (!result.source_id.empty()) {
          texture_item_ids_by_source.erase(result.source_id);
        }
        return item_id;
      }
    }

    if (!result.source_id.empty()) {
      const auto it = texture_item_ids_by_source.find(result.source_id);
      if (it != texture_item_ids_by_source.end()) {
        const auto item_id = it->second;
        texture_item_ids_by_source.erase(it);
        if (!result.texture_id.empty()) {
          texture_item_ids.erase(result.texture_id);
        }
        if (result.source_key != nullptr) {
          texture_item_ids_by_key.erase(result.source_key);
        }
        return item_id;
      }
    }

    return std::nullopt;
  };

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

  for (const auto& step : context.steps) {
    auto& tracker = context.planner.Tracker(step.item_id);
    if (tracker.IsReady()) {
      const auto& item = context.planner.Item(step.item_id);
      scheduler->AddReady(step.item_id, item.kind);
    }
  }

  auto enqueue_ready = [&](const PlanItemId item_id) {
    const auto u_item = item_id.get();
    if (submitted[u_item] != 0U) {
      return;
    }
    const auto& item = context.planner.Item(item_id);
    scheduler->AddReady(item_id, item.kind);
  };

  auto mark_complete = [&](const PlanItemId item_id, const PlanItemKind kind,
                         std::string_view item_name) {
    const auto u_item = item_id.get();
    if (completed[u_item] != 0U) {
      return;
    }
    completed[u_item] = 1U;
    ++completed_count;
    if (progress_.has_value() && progress_->on_progress) {
      const float overall_progress = (item_count > 0U)
        ? progress_->overall_start
          + (progress_->overall_end - progress_->overall_start)
            * (static_cast<float>(completed_count)
              / static_cast<float>(item_count))
        : progress_->overall_end;
      const auto label = kind_label(kind);
      progress_->ReportItemProgress(ProgressEventKind::kItemFinished,
        phase_for_kind(kind), overall_progress,
        std::string(item_name) + " finished", label, std::string(item_name));
    }
    for (const auto dependent : dependents[u_item]) {
      auto& tracker = context.planner.Tracker(dependent);
      if (tracker.MarkReady({ item_id })) {
        enqueue_ready(dependent);
      }
    }
  };

  auto MakeItemStartedCallback
    = [&](const PlanItemKind kind,
        std::string_view item_name) -> std::function<void()> {
    if (!progress_.has_value() || !progress_->on_progress) {
      return {};
    }
    const std::string name(item_name);
    return [&, kind, name]() {
      const float overall_progress = (item_count > 0U)
        ? progress_->overall_start
          + (progress_->overall_end - progress_->overall_start)
            * (static_cast<float>(completed_count)
              / static_cast<float>(item_count))
        : progress_->overall_start;
      const auto label = kind_label(kind);
      progress_->ReportItemProgress(ProgressEventKind::kItemStarted,
        phase_for_kind(kind), overall_progress, name + " started", label, name);
    };
  };

  auto ReportItemFinished
    = [&](const PlanItemKind kind, std::string_view item_name) -> void {
    if (!progress_.has_value() || !progress_->on_progress) {
      return;
    }
    const float overall_progress = (item_count > 0U) ? progress_->overall_start
        + (progress_->overall_end - progress_->overall_start)
          * (static_cast<float>(completed_count)
            / static_cast<float>(item_count))
                                                     : progress_->overall_start;
    const auto label = kind_label(kind);
    progress_->ReportItemProgress(ProgressEventKind::kItemFinished,
      phase_for_kind(kind), overall_progress,
      std::string(item_name) + " finished", label, std::string(item_name));
  };

  auto ReportItemCollected
    = [&](const PlanItemKind kind, const size_t queue_size,
        const size_t queue_capacity) -> void {
    if (!progress_.has_value() || !progress_->on_progress) {
      return;
    }

    const float overall_progress = (item_count > 0U) ? progress_->overall_start
        + (progress_->overall_end - progress_->overall_start)
          * (static_cast<float>(completed_count)
            / static_cast<float>(item_count))
                                                     : progress_->overall_start;
    const auto label = kind_label(kind);
    const float queue_load = (queue_capacity > 0U)
      ? static_cast<float>(queue_size) / static_cast<float>(queue_capacity)
      : 1.0f;
    progress_->ReportItemCollected(
      phase_for_kind(kind), overall_progress, {}, label, queue_load);
  };

  enum class GeometryBufferKind : uint8_t {
    kVertex,
    kIndex,
    kJointIndex,
    kJointWeight,
    kInverseBind,
    kJointRemap,
  };

  const auto MakeGeometryBufferId
    = [&](std::string_view source_id, std::string_view suffix,
        const size_t lod_index) -> std::string {
    std::ostringstream id;
    id << "geom-buffer:" << source_id << ":lod" << lod_index << ":";
    id << suffix;
    return id.str();
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

  auto resolve_mesh_build_item
    = [&](const MeshBuildPipeline::WorkResult& result)
    -> std::optional<PlanItemId> {
    if (result.source_key != nullptr) {
      const auto it = mesh_build_item_ids_by_key.find(result.source_key);
      if (it != mesh_build_item_ids_by_key.end()) {
        const auto item_id = it->second;
        mesh_build_item_ids_by_key.erase(it);
        if (!result.source_id.empty()) {
          mesh_build_item_ids_by_source.erase(result.source_id);
        }
        return item_id;
      }
    }

    if (!result.source_id.empty()) {
      const auto it = mesh_build_item_ids_by_source.find(result.source_id);
      if (it != mesh_build_item_ids_by_source.end()) {
        const auto item_id = it->second;
        mesh_build_item_ids_by_source.erase(it);
        if (result.source_key != nullptr) {
          mesh_build_item_ids_by_key.erase(result.source_key);
        }
        return item_id;
      }
    }

    return std::nullopt;
  };

  auto resolve_geometry_item =
    [&](
      const GeometryPipeline::WorkResult& result) -> std::optional<PlanItemId> {
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
      if (const auto item_id = resolve_texture_item(result)) {
        const auto& item = context.planner.Item(*item_id);
        ReportItemFinished(item.kind, item.debug_name);
      } else if (!result.source_id.empty()) {
        ReportItemFinished(PlanItemKind::kTextureResource, result.source_id);
      }
      co_return false;
    }

    if (!result.source_id.empty()) {
      texture_indices.insert_or_assign(result.source_id, *index);
    }

    if (const auto item_id = resolve_texture_item(result)) {
      const auto& item = context.planner.Item(*item_id);
      mark_complete(*item_id, item.kind, item.debug_name);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.texture_unmapped",
      "Texture result could not be mapped to a plan item", result.source_id,
      ""));
    if (!result.source_id.empty()) {
      ReportItemFinished(PlanItemKind::kTextureResource, result.source_id);
    }
    co_return false;
  };

  struct PendingGeometry final {
    MeshBuildPipeline::WorkResult result;
    std::vector<MeshBufferBindings> bindings;
    std::optional<PlanItemId> item_id;
  };

  auto process_buffer_result
    = [&](BufferPipeline::WorkResult result) -> co::Co<bool> {
    const auto emitted = EmitBufferPayload(result);
    if (!emitted.has_value()) {
      if (!result.source_id.empty()) {
        ReportItemFinished(PlanItemKind::kBufferResource, result.source_id);
      }
      co_return false;
    }

    if (const auto item_id = resolve_buffer_item(result)) {
      const auto& item = context.planner.Item(*item_id);
      mark_complete(*item_id, item.kind, item.debug_name);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.buffer_unmapped",
      "Buffer result could not be mapped to a plan item", result.source_id,
      ""));
    if (!result.source_id.empty()) {
      ReportItemFinished(PlanItemKind::kBufferResource, result.source_id);
    }
    co_return false;
  };

  auto process_material_result
    = [&](MaterialPipeline::WorkResult result) -> co::Co<bool> {
    if (!EmitMaterialPayload(result)) {
      if (const auto item_id = resolve_material_item(result)) {
        const auto& item = context.planner.Item(*item_id);
        ReportItemFinished(item.kind, item.debug_name);
      } else if (!result.source_id.empty()) {
        ReportItemFinished(PlanItemKind::kMaterialAsset, result.source_id);
      }
      co_return false;
    }

    if (const auto item_id = resolve_material_item(result)) {
      if (result.cooked.has_value()) {
        material_keys.emplace(*item_id, result.cooked->material_key);
      }
      const auto& item = context.planner.Item(*item_id);
      mark_complete(*item_id, item.kind, item.debug_name);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.material_unmapped",
      "Material result could not be mapped to a plan item", result.source_id,
      ""));
    if (!result.source_id.empty()) {
      ReportItemFinished(PlanItemKind::kMaterialAsset, result.source_id);
    }
    co_return false;
  };

  auto process_mesh_build_result
    = [&](MeshBuildPipeline::WorkResult result) -> co::Co<bool> {
    const auto item_id = resolve_mesh_build_item(result);
    if (!item_id.has_value()) {
      session_.AddDiagnostic(
        MakeErrorDiagnostic("import.plan.mesh_build_unmapped",
          "Mesh build result could not be mapped to a plan item",
          result.source_id, ""));
      if (!result.source_id.empty()) {
        ReportItemFinished(PlanItemKind::kMeshBuild, result.source_id);
      }
      co_return false;
    }
    if (!result.success || !result.cooked.has_value()) {
      AddDiagnostics(session_, std::move(result.diagnostics));
      const auto& item = context.planner.Item(*item_id);
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    AddDiagnostics(session_, std::move(result.diagnostics));

    auto& buffer_emitter = session_.BufferEmitter();
    const auto EmitGeometryBuffer
      = [&](CookedBufferPayload payload, const GeometryBufferKind kind,
          const size_t lod_index, std::string_view suffix,
          std::vector<MeshBufferBindings>& bindings) -> bool {
      const auto buffer_id
        = MakeGeometryBufferId(result.source_id, suffix, lod_index);
      if (const auto started
        = MakeItemStartedCallback(PlanItemKind::kMeshBuild, buffer_id);
        started) {
        started();
      }

      const auto emitted = buffer_emitter.Emit(std::move(payload));
      auto& lod_binding = bindings[lod_index];
      switch (kind) {
      case GeometryBufferKind::kVertex:
        lod_binding.vertex_buffer = emitted;
        break;
      case GeometryBufferKind::kIndex:
        lod_binding.index_buffer = emitted;
        break;
      case GeometryBufferKind::kJointIndex:
        lod_binding.joint_index_buffer = emitted;
        break;
      case GeometryBufferKind::kJointWeight:
        lod_binding.joint_weight_buffer = emitted;
        break;
      case GeometryBufferKind::kInverseBind:
        lod_binding.inverse_bind_buffer = emitted;
        break;
      case GeometryBufferKind::kJointRemap:
        lod_binding.joint_remap_buffer = emitted;
        break;
      }

      ReportItemFinished(PlanItemKind::kMeshBuild, buffer_id);
      return true;
    };

    if (!result.cooked.has_value()) {
      const auto& item = context.planner.Item(*item_id);
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    auto& cooked = *result.cooked;
    std::vector<MeshBufferBindings> bindings;
    bindings.resize(cooked.lods.size());

    for (size_t lod_index = 0; lod_index < cooked.lods.size(); ++lod_index) {
      auto& lod = cooked.lods[lod_index];
      if (!lod.auxiliary_buffers.empty()
        && lod.auxiliary_buffers.size() != 4u) {
        session_.AddDiagnostic(MakeErrorDiagnostic("mesh.aux_buffer_count",
          "Unexpected auxiliary buffer count for mesh LOD", result.source_id,
          ""));
        co_return false;
      }
      if (!EmitGeometryBuffer(std::move(lod.vertex_buffer),
            GeometryBufferKind::kVertex, lod_index, "vb", bindings)) {
        co_return false;
      }
      if (!EmitGeometryBuffer(std::move(lod.index_buffer),
            GeometryBufferKind::kIndex, lod_index, "ib", bindings)) {
        co_return false;
      }
      if (lod.auxiliary_buffers.size() == 4u) {
        if (!EmitGeometryBuffer(std::move(lod.auxiliary_buffers[0]),
              GeometryBufferKind::kJointIndex, lod_index, "joint_indices",
              bindings)) {
          co_return false;
        }
        if (!EmitGeometryBuffer(std::move(lod.auxiliary_buffers[1]),
              GeometryBufferKind::kJointWeight, lod_index, "joint_weights",
              bindings)) {
          co_return false;
        }
        if (!EmitGeometryBuffer(std::move(lod.auxiliary_buffers[2]),
              GeometryBufferKind::kInverseBind, lod_index, "inverse_bind",
              bindings)) {
          co_return false;
        }
        if (!EmitGeometryBuffer(std::move(lod.auxiliary_buffers[3]),
              GeometryBufferKind::kJointRemap, lod_index, "joint_remap",
              bindings)) {
          co_return false;
        }
      }
    }

    mesh_build_results.emplace(*item_id,
      MeshBuildReady {
        .result = std::move(result), .bindings = std::move(bindings) });

    const auto& item = context.planner.Item(*item_id);
    mark_complete(*item_id, item.kind, item.debug_name);
    co_return true;
  };

  auto process_geometry_result
    = [&](GeometryPipeline::WorkResult result) -> co::Co<bool> {
    const auto item_id = resolve_geometry_item(result);
    if (!item_id.has_value()) {
      session_.AddDiagnostic(
        MakeErrorDiagnostic("import.plan.geometry_unmapped",
          "Geometry result could not be mapped to a plan item",
          result.source_id, ""));
      if (!result.source_id.empty()) {
        ReportItemFinished(PlanItemKind::kGeometryAsset, result.source_id);
      }
      co_return false;
    }

    if (!result.success || !result.cooked.has_value()
      || result.finalized_descriptor_bytes.empty()) {
      AddDiagnostics(session_, std::move(result.diagnostics));
      const auto& item = context.planner.Item(*item_id);
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    AddDiagnostics(session_, std::move(result.diagnostics));
    if (!EmitGeometryPayload(
          *result.cooked, result.finalized_descriptor_bytes)) {
      const auto& item = context.planner.Item(*item_id);
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    geometry_keys.emplace(*item_id, result.cooked->geometry_key);
    const auto& item = context.planner.Item(*item_id);
    mark_complete(*item_id, item.kind, item.debug_name);
    co_return true;
  };

  auto process_scene_result
    = [&](ScenePipeline::WorkResult result) -> co::Co<bool> {
    const auto item_id = resolve_scene_item(result);
    if (!EmitScenePayload(std::move(result))) {
      co_return false;
    }

    if (item_id.has_value()) {
      const auto& item = context.planner.Item(*item_id);
      mark_complete(*item_id, item.kind, item.debug_name);
      co_return true;
    }

    session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.scene_unmapped",
      "Scene result could not be mapped to a plan item", "", ""));
    co_return false;
  };

  auto ProcessEnvelope = [&](ResultEnvelope envelope) -> co::Co<bool> {
    pending_envelopes.fetch_sub(1, std::memory_order_acq_rel);
    switch (envelope.kind) {
    case ResultKind::kTexture:
      co_return co_await process_texture_result(
        std::get<TexturePipeline::WorkResult>(std::move(envelope.payload)));
    case ResultKind::kBuffer:
      co_return co_await process_buffer_result(
        std::get<BufferPipeline::WorkResult>(std::move(envelope.payload)));
    case ResultKind::kMaterial:
      co_return co_await process_material_result(
        std::get<MaterialPipeline::WorkResult>(std::move(envelope.payload)));
    case ResultKind::kMeshBuild:
      co_return co_await process_mesh_build_result(
        std::get<MeshBuildPipeline::WorkResult>(std::move(envelope.payload)));
    case ResultKind::kGeometry:
      co_return co_await process_geometry_result(
        std::get<GeometryPipeline::WorkResult>(std::move(envelope.payload)));
    case ResultKind::kScene:
      co_return co_await process_scene_result(
        std::get<ScenePipeline::WorkResult>(std::move(envelope.payload)));
    }

    co_return false;
  };

  std::atomic<bool> collector_done { false };
  co::Event collector_finished;
  nursery.Start([&]() -> co::Co<> {
    size_t collect_cursor = 0U;
    while (true) {
      if (collector_done.load(std::memory_order_acquire)
        && PendingTotal() == 0U) {
        break;
      }

      if (PendingTotal() == 0U) {
        auto kick = co_await collector_kick.Receive();
        if (!kick.has_value()) {
          break;
        }
        continue;
      }

      std::optional<ResultEnvelope> envelope;
      for (size_t attempt = 0U; attempt < kPlanKindCount; ++attempt) {
        const auto index = (collect_cursor + attempt) % kPlanKindCount;
        const auto kind = static_cast<PlanItemKind>(index);
        switch (kind) {
        case PlanItemKind::kTextureResource:
          if (pending_textures.load(std::memory_order_acquire) == 0U
            || !texture_pipeline_) {
            continue;
          }
          {
            auto result = co_await texture_pipeline_->Collect();
            pending_textures.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kTextureResource,
              texture_pipeline_->OutputQueueSize(),
              texture_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kTexture,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kBufferResource:
          if (pending_buffers.load(std::memory_order_acquire) == 0U
            || !buffer_pipeline_) {
            continue;
          }
          {
            auto result = co_await buffer_pipeline_->Collect();
            pending_buffers.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kBufferResource,
              buffer_pipeline_->OutputQueueSize(),
              buffer_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kBuffer,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kMaterialAsset:
          if (pending_materials.load(std::memory_order_acquire) == 0U
            || !material_pipeline_) {
            continue;
          }
          {
            auto result = co_await material_pipeline_->Collect();
            pending_materials.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kMaterialAsset,
              material_pipeline_->OutputQueueSize(),
              material_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kMaterial,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kMeshBuild:
          if (pending_mesh_builds.load(std::memory_order_acquire) == 0U
            || !mesh_build_pipeline_) {
            continue;
          }
          {
            auto result = co_await mesh_build_pipeline_->Collect();
            pending_mesh_builds.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kMeshBuild,
              mesh_build_pipeline_->OutputQueueSize(),
              mesh_build_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kMeshBuild,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kGeometryAsset:
          if (pending_geometries.load(std::memory_order_acquire) == 0U
            || !geometry_pipeline_) {
            continue;
          }
          {
            auto result = co_await geometry_pipeline_->Collect();
            pending_geometries.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kGeometryAsset,
              geometry_pipeline_->OutputQueueSize(),
              geometry_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kGeometry,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kSceneAsset:
          if (pending_scenes.load(std::memory_order_acquire) == 0U
            || !scene_pipeline_) {
            continue;
          }
          {
            auto result = co_await scene_pipeline_->Collect();
            pending_scenes.fetch_sub(1, std::memory_order_acq_rel);
            ReportItemCollected(PlanItemKind::kSceneAsset,
              scene_pipeline_->OutputQueueSize(),
              scene_pipeline_->OutputQueueCapacity());
            envelope = ResultEnvelope {
              .kind = ResultKind::kScene,
              .payload = std::move(result),
            };
          }
          break;
        case PlanItemKind::kAudioResource:
          continue;
        }

        if (envelope.has_value()) {
          collect_cursor = (index + 1U) % kPlanKindCount;
          break;
        }
      }

      if (!envelope.has_value()) {
        continue;
      }

      pending_envelopes.fetch_add(1, std::memory_order_acq_rel);
      if (!co_await result_channel.Send(std::move(*envelope))) {
        pending_envelopes.fetch_sub(1, std::memory_order_acq_rel);
        break;
      }
    }

    result_channel.Close();
    collector_finished.Trigger();
    co_return;
  });

  auto Finish = [&](const bool ok) -> co::Co<bool> {
    collector_done.store(true, std::memory_order_release);
    collector_kick.Close();
    co_await collector_finished;
    co_return ok;
  };

  auto submit_texture = [&](const PlanItemId item_id,
                          std::function<void()> on_started) -> co::Co<bool> {
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

    if (auto* source_bytes
      = std::get_if<TexturePipeline::SourceBytes>(&payload.item.source)) {
      if (!payload.item.source_path.empty()) {
        auto reader = session_.FileReader();
        const auto source_path_string = payload.item.source_path.string();
        if (reader == nullptr) {
          session_.AddDiagnostic(MakeErrorDiagnostic("import.file_reader",
            "Import session has no async file reader", payload.item.source_id,
            source_path_string));
        } else {
          auto read_result
            = co_await reader.get()->ReadFile(payload.item.source_path);
          if (!read_result.has_value()) {
            const auto message = "Failed to read texture file: "
              + read_result.error().ToString();
            session_.AddDiagnostic(
              MakeWarningDiagnostic("import.texture.load_failed", message,
                payload.item.source_id, source_path_string));
          } else {
            auto bytes = std::make_shared<std::vector<std::byte>>(
              std::move(read_result.value()));
            payload.item.source = TexturePipeline::SourceBytes {
              .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
              .owner = std::static_pointer_cast<const void>(bytes),
            };
          }
        }
      }
    }
    const auto total_before = PendingTotal();
    pending_textures.fetch_add(1, std::memory_order_acq_rel);
    payload.item.on_started = std::move(on_started);
    co_await pipeline.Submit(std::move(payload.item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_buffer = [&](const PlanItemId item_id,
                         std::function<void()> on_started) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Buffer(item.work_handle);
    if (!payload.item.source_id.empty()) {
      buffer_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }

    auto& pipeline = EnsureBufferPipeline(nursery);
    const auto total_before = PendingTotal();
    pending_buffers.fetch_add(1, std::memory_order_acq_rel);
    payload.item.on_started = std::move(on_started);
    co_await pipeline.Submit(std::move(payload.item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_material = [&](const PlanItemId item_id,
                           std::function<void()> on_started) -> co::Co<bool> {
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
    const auto total_before = PendingTotal();
    pending_materials.fetch_add(1, std::memory_order_acq_rel);
    payload.item.on_started = std::move(on_started);
    co_await pipeline.Submit(std::move(payload.item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_mesh_build = [&](const PlanItemId item_id,
                             std::function<void()> on_started) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.MeshBuild(item.work_handle);
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
      mesh_build_item_ids_by_source.insert_or_assign(
        payload.item.source_id, item_id);
    }
    if (payload.item.source_key != nullptr) {
      mesh_build_item_ids_by_key.insert_or_assign(
        payload.item.source_key, item_id);
    }

    auto& pipeline = EnsureMeshBuildPipeline(nursery);
    const auto total_before = PendingTotal();
    pending_mesh_builds.fetch_add(1, std::memory_order_acq_rel);
    payload.item.on_started = std::move(on_started);
    co_await pipeline.Submit(std::move(payload.item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_geometry_asset
    = [&](const PlanItemId item_id,
        std::function<void()> on_started) -> co::Co<bool> {
    auto& item = context.planner.Item(item_id);
    auto& payload = context.payloads.Geometry(item.work_handle);

    const auto result_it
      = mesh_build_results.find(payload.item.mesh_build_item);
    if (result_it == mesh_build_results.end()) {
      session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.mesh_missing",
        "Missing mesh build result for geometry finalize", item.debug_name,
        ""));
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    PendingGeometry pending;
    pending.result = std::move(result_it->second.result);
    pending.bindings = std::move(result_it->second.bindings);
    pending.item_id = item_id;
    mesh_build_results.erase(result_it);

    if (!pending.result.success || !pending.result.cooked.has_value()) {
      AddDiagnostics(session_, std::move(pending.result.diagnostics));
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }

    AddDiagnostics(session_, std::move(pending.result.diagnostics));

    GeometryPipeline::WorkItem geometry_item;
    geometry_item.source_id = pending.result.source_id;
    geometry_item.bindings = std::move(pending.bindings);
    bool missing_material = false;
    auto& cooked_payload = *pending.result.cooked;
    geometry_item.material_patches.reserve(
      cooked_payload.material_patch_offsets.size());
    for (const auto& patch_offset : cooked_payload.material_patch_offsets) {
      const auto slot = patch_offset.slot;
      if (slot >= context.material_slots.size()) {
        session_.AddDiagnostic(
          MakeErrorDiagnostic("import.plan.material_slot_invalid",
            "Material slot is outside the plan material list", item.debug_name,
            std::to_string(slot)));
        missing_material = true;
        continue;
      }

      const auto material_item = context.material_slots[slot];
      const auto it = material_keys.find(material_item);
      if (it == material_keys.end()) {
        session_.AddDiagnostic(
          MakeErrorDiagnostic("import.plan.material_key_missing",
            "Missing material key for geometry patch", item.debug_name,
            std::to_string(slot)));
        missing_material = true;
        continue;
      }

      geometry_item.material_patches.push_back(
        GeometryPipeline::MaterialKeyPatch {
          .material_key_offset = patch_offset.material_key_offset,
          .key = it->second,
        });
    }
    if (missing_material) {
      ReportItemFinished(item.kind, item.debug_name);
      co_return false;
    }
    geometry_item.cooked = std::move(cooked_payload);
    geometry_item.on_started = std::move(on_started);
    geometry_item.stop_token = stop_token_;

    if (!geometry_item.source_id.empty()) {
      geometry_item_ids_by_source.insert_or_assign(
        geometry_item.source_id, item_id);
    }

    auto& pipeline = EnsureGeometryPipeline(nursery);
    const auto total_before = PendingTotal();
    pending_geometries.fetch_add(1, std::memory_order_acq_rel);
    co_await pipeline.Submit(std::move(geometry_item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_scene = [&](const PlanItemId item_id,
                        std::function<void()> on_started) -> co::Co<bool> {
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
    const auto total_before = PendingTotal();
    pending_scenes.fetch_add(1, std::memory_order_acq_rel);
    payload.item.on_started = std::move(on_started);
    co_await pipeline.Submit(std::move(payload.item));
    if (total_before == 0U) {
      NotifyCollector();
    }
    co_return true;
  };

  auto submit_item = [&](const PlanItemId item_id) -> co::Co<bool> {
    const auto u_item = item_id.get();
    if (submitted[u_item] != 0U) {
      co_return true;
    }
    submitted[u_item] = 1U;

    const auto& item = context.planner.Item(item_id);
    auto on_started = MakeItemStartedCallback(item.kind, item.debug_name);
    switch (item.kind) {
    case PlanItemKind::kTextureResource:
      co_return co_await submit_texture(item_id, std::move(on_started));
    case PlanItemKind::kBufferResource:
      co_return co_await submit_buffer(item_id, std::move(on_started));
    case PlanItemKind::kMaterialAsset:
      co_return co_await submit_material(item_id, std::move(on_started));
    case PlanItemKind::kMeshBuild:
      co_return co_await submit_mesh_build(item_id, std::move(on_started));
    case PlanItemKind::kGeometryAsset:
      co_return co_await submit_geometry_asset(item_id, std::move(on_started));
    case PlanItemKind::kSceneAsset:
      co_return co_await submit_scene(item_id, std::move(on_started));
    case PlanItemKind::kAudioResource:
      session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.unhandled_kind",
        "Unhandled plan item kind in import", item.debug_name, ""));
      co_return false;
    }

    co_return false;
  };

  auto KindHasCapacity = [&](const PlanItemKind kind) -> bool {
    switch (kind) {
    case PlanItemKind::kTextureResource:
      return pending_textures.load(std::memory_order_acquire)
        < concurrency_.texture.queue_capacity;
    case PlanItemKind::kBufferResource:
      return pending_buffers.load(std::memory_order_acquire)
        < concurrency_.buffer.queue_capacity;
    case PlanItemKind::kMaterialAsset:
      return pending_materials.load(std::memory_order_acquire)
        < concurrency_.material.queue_capacity;
    case PlanItemKind::kMeshBuild:
      return pending_mesh_builds.load(std::memory_order_acquire)
        < concurrency_.mesh_build.queue_capacity;
    case PlanItemKind::kGeometryAsset:
      return pending_geometries.load(std::memory_order_acquire)
        < concurrency_.geometry.queue_capacity;
    case PlanItemKind::kSceneAsset:
      return pending_scenes.load(std::memory_order_acquire)
        < concurrency_.scene.queue_capacity;
    case PlanItemKind::kAudioResource:
      return false;
    }
    return false;
  };

  auto BuildAvailability = [&]() -> KindAvailability {
    KindAvailability availability {};
    for (size_t index = 0U; index < availability.size(); ++index) {
      const auto kind = static_cast<PlanItemKind>(index);
      availability[index] = KindHasCapacity(kind);
    }
    return availability;
  };

  while (completed_count < item_count) {
    bool submitted_any = false;
    auto availability = BuildAvailability();
    auto next_item = scheduler->NextReady(availability);
    while (next_item.has_value()) {
      submitted_any = true;
      if (!co_await submit_item(*next_item)) {
        co_return co_await Finish(false);
      }
      availability = BuildAvailability();
      next_item = scheduler->NextReady(availability);
    }

    if (stop_token_.stop_requested()) {
      co_return co_await Finish(false);
    }

    while (auto envelope = result_channel.TryReceive()) {
      if (!co_await ProcessEnvelope(std::move(*envelope))) {
        co_return co_await Finish(false);
      }
    }

    if (completed_count >= item_count) {
      co_return co_await Finish(true);
    }

    const auto pending_total = PendingTotal();
    const auto pending_envelopes_count
      = pending_envelopes.load(std::memory_order_acquire);
    if (pending_total == 0U && pending_envelopes_count == 0U) {
      if (scheduler->HasReady()) {
        const auto any_capacity = [&]() -> bool {
          for (size_t index = 0U; index < kPlanKindCount; ++index) {
            if (KindHasCapacity(static_cast<PlanItemKind>(index))) {
              return true;
            }
          }
          return false;
        }();

        if (!any_capacity) {
          LOG_F(INFO,
            "plan capacity blocked: ready={} completed={}/{} submitted={} "
            "pending={{tex={}, buf={}, mat={}, mesh={}, geo={}, scene={}, "
            "env={}}}",
            scheduler->HasReady(), completed_count, item_count,
            std::accumulate(submitted.begin(), submitted.end(), 0U),
            pending_textures.load(std::memory_order_acquire),
            pending_buffers.load(std::memory_order_acquire),
            pending_materials.load(std::memory_order_acquire),
            pending_mesh_builds.load(std::memory_order_acquire),
            pending_geometries.load(std::memory_order_acquire),
            pending_scenes.load(std::memory_order_acquire),
            pending_envelopes_count);
          session_.AddDiagnostic(
            MakeErrorDiagnostic("import.plan.capacity_blocked",
              "Import plan has ready work but no pipeline capacity available",
              "", ""));
          co_return co_await Finish(false);
        }

        continue;
      }

      LOG_F(INFO,
        "plan deadlock: completed={}/{} submitted={} ready={} "
        "pending={{tex={}, buf={}, mat={}, mesh={}, geo={}, scene={}, "
        "env={}}}",
        completed_count, item_count,
        std::accumulate(submitted.begin(), submitted.end(), 0U),
        scheduler->HasReady(), pending_textures.load(std::memory_order_acquire),
        pending_buffers.load(std::memory_order_acquire),
        pending_materials.load(std::memory_order_acquire),
        pending_mesh_builds.load(std::memory_order_acquire),
        pending_geometries.load(std::memory_order_acquire),
        pending_scenes.load(std::memory_order_acquire),
        pending_envelopes_count);
      LOG_F(INFO,
        "plan deadlock maps: textures={{id={}, key={}, source={}}} "
        "materials={{source={}}} mesh_build={{source={}, key={}}} "
        "geometry={{source={}}} scene={{source={}}}",
        texture_item_ids.size(), texture_item_ids_by_key.size(),
        texture_item_ids_by_source.size(), material_item_ids_by_source.size(),
        mesh_build_item_ids_by_source.size(), mesh_build_item_ids_by_key.size(),
        geometry_item_ids_by_source.size(), scene_item_ids_by_source.size());
      session_.AddDiagnostic(MakeErrorDiagnostic("import.plan.deadlock",
        "Import plan has no pending work but is not complete", "", ""));
      co_return co_await Finish(false);
    }

    if (!submitted_any) {
      auto envelope = co_await result_channel.Receive();
      if (!envelope.has_value()) {
        co_return co_await Finish(false);
      }
      if (!co_await ProcessEnvelope(std::move(*envelope))) {
        co_return co_await Finish(false);
      }
    }
  }

  co_return co_await Finish(true);
}

} // namespace oxygen::content::import::detail
