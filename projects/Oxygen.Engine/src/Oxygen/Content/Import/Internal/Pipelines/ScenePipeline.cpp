//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Internal/Pipelines/ScenePipeline.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::AssetKey;
  using data::AssetType;
  using data::ComponentType;
  using data::pak::DirectionalLightRecord;
  using data::pak::NodeRecord;
  using data::pak::OrthographicCameraRecord;
  using data::pak::PerspectiveCameraRecord;
  using data::pak::PointLightRecord;
  using data::pak::RenderableRecord;
  using data::pak::SceneAssetDesc;
  using data::pak::SceneComponentTableDesc;
  using data::pak::SceneEnvironmentBlockHeader;
  using data::pak::SceneEnvironmentSystemRecordHeader;
  using data::pak::SpotLightRecord;

  struct BuildOutcome {
    std::vector<std::byte> bytes;
    std::vector<ImportDiagnostic> diagnostics;
    bool canceled = false;
    bool success = false;
  };

  struct StageRunOutcome {
    SceneStageResult result;
    std::vector<ImportDiagnostic> diagnostics;
    bool canceled = false;
  };

  [[nodiscard]] auto MakeCancelDiagnostic(std::string_view source_id)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "import.canceled",
      .message = "Import canceled",
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

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

  [[nodiscard]] auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto BuildSceneAssetKey(
    const std::string_view virtual_path, AssetKeyPolicy policy) -> AssetKey
  {
    switch (policy) {
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(virtual_path);
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    }
    return util::MakeDeterministicAssetKey(virtual_path);
  }

  auto SortSceneComponents(SceneBuild& build) -> void
  {
    std::ranges::sort(build.renderables,
      [](const RenderableRecord& a, const RenderableRecord& b) {
        return a.node_index < b.node_index;
      });

    std::ranges::sort(build.perspective_cameras,
      [](const PerspectiveCameraRecord& a, const PerspectiveCameraRecord& b) {
        return a.node_index < b.node_index;
      });

    std::ranges::sort(build.orthographic_cameras,
      [](const OrthographicCameraRecord& a, const OrthographicCameraRecord& b) {
        return a.node_index < b.node_index;
      });

    std::ranges::sort(build.directional_lights,
      [](const DirectionalLightRecord& a, const DirectionalLightRecord& b) {
        return a.node_index < b.node_index;
      });

    std::ranges::sort(build.point_lights,
      [](const PointLightRecord& a, const PointLightRecord& b) {
        return a.node_index < b.node_index;
      });

    std::ranges::sort(build.spot_lights,
      [](const SpotLightRecord& a, const SpotLightRecord& b) {
        return a.node_index < b.node_index;
      });
  }

  [[nodiscard]] auto SerializeScene(const std::string_view scene_name,
    const AssetKey scene_key, const SceneBuild& build,
    std::span<const SceneEnvironmentSystem> environment_systems,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id)
    -> BuildOutcome
  {
    BuildOutcome outcome;

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);

    SceneAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kScene);
    util::TruncateAndNullTerminate(
      desc.header.name, sizeof(desc.header.name), scene_name);
    desc.header.version = data::pak::kSceneAssetVersion;
    desc.header.content_hash = 0;
    desc.nodes.offset = sizeof(SceneAssetDesc);
    desc.nodes.count = static_cast<uint32_t>(build.nodes.size());
    desc.nodes.entry_size = sizeof(NodeRecord);

    const auto nodes_bytes = std::as_bytes(std::span(build.nodes));

    desc.scene_strings.offset = static_cast<data::pak::StringTableOffsetT>(
      sizeof(SceneAssetDesc) + nodes_bytes.size());
    desc.scene_strings.size
      = static_cast<data::pak::StringTableSizeT>(build.strings.size());

    struct ComponentTablePayload {
      SceneComponentTableDesc desc {};
      std::span<const std::byte> bytes {};
    };

    std::vector<ComponentTablePayload> component_tables;
    component_tables.reserve(6);

    auto add_component_table = [&](const ComponentType type,
                                 const size_t entry_size,
                                 std::span<const std::byte> bytes) {
      if (bytes.empty()) {
        return;
      }

      SceneComponentTableDesc table_desc {};
      table_desc.component_type = static_cast<uint32_t>(type);
      table_desc.table.entry_size = static_cast<uint32_t>(entry_size);
      table_desc.table.count = static_cast<uint32_t>(bytes.size() / entry_size);

      component_tables.push_back({ .desc = table_desc, .bytes = bytes });
    };

    add_component_table(ComponentType::kRenderable, sizeof(RenderableRecord),
      std::as_bytes(std::span(build.renderables)));
    add_component_table(ComponentType::kPerspectiveCamera,
      sizeof(PerspectiveCameraRecord),
      std::as_bytes(std::span(build.perspective_cameras)));
    add_component_table(ComponentType::kOrthographicCamera,
      sizeof(OrthographicCameraRecord),
      std::as_bytes(std::span(build.orthographic_cameras)));
    add_component_table(ComponentType::kDirectionalLight,
      sizeof(DirectionalLightRecord),
      std::as_bytes(std::span(build.directional_lights)));
    add_component_table(ComponentType::kPointLight, sizeof(PointLightRecord),
      std::as_bytes(std::span(build.point_lights)));
    add_component_table(ComponentType::kSpotLight, sizeof(SpotLightRecord),
      std::as_bytes(std::span(build.spot_lights)));

    std::vector<SceneComponentTableDesc> component_directory;

    if (!component_tables.empty()) {
      size_t payload_cursor = static_cast<size_t>(desc.scene_strings.offset)
        + desc.scene_strings.size;
      desc.component_table_directory_offset = payload_cursor;
      desc.component_table_count
        = static_cast<uint32_t>(component_tables.size());

      payload_cursor
        += component_tables.size() * sizeof(SceneComponentTableDesc);

      component_directory.reserve(component_tables.size());
      for (auto& table : component_tables) {
        table.desc.table.offset = payload_cursor;
        payload_cursor += table.bytes.size();
        component_directory.push_back(table.desc);
      }
    } else {
      desc.component_table_directory_offset = 0;
      desc.component_table_count = 0;
    }

    static_cast<void>(scene_key);

    if (auto result = writer.WriteBlob(
          std::as_bytes(std::span<const SceneAssetDesc, 1>(&desc, 1)));
      !result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
        "Failed to write scene header", source_id, {}));
      return outcome;
    }

    if (auto result = writer.WriteBlob(nodes_bytes); !result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "scene.serialize_failed", "Failed to write nodes", source_id, {}));
      return outcome;
    }

    if (auto result = writer.WriteBlob(std::span(build.strings));
      !result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
        "Failed to write string table", source_id, {}));
      return outcome;
    }

    if (!component_tables.empty()) {
      const auto directory_span = std::span<const SceneComponentTableDesc>(
        component_directory.data(), component_directory.size());
      if (auto result = writer.WriteBlob(std::as_bytes(directory_span));
        !result.has_value()) {
        diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
          "Failed to write component table directory", source_id, {}));
        return outcome;
      }

      for (const auto& table : component_tables) {
        if (auto result = writer.WriteBlob(table.bytes); !result.has_value()) {
          diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
            "Failed to write component table", source_id, {}));
          return outcome;
        }
      }
    }

    SceneEnvironmentBlockHeader env_header {};
    env_header.byte_size = sizeof(SceneEnvironmentBlockHeader);
    env_header.systems_count = 0;

    for (const auto& system : environment_systems) {
      if (system.record_bytes.size()
        < sizeof(SceneEnvironmentSystemRecordHeader)) {
        diagnostics.push_back(
          MakeErrorDiagnostic("scene.environment.record_too_small",
            "Environment system record is too small", source_id, {}));
        return outcome;
      }

      SceneEnvironmentSystemRecordHeader header {};
      std::memcpy(&header, system.record_bytes.data(), sizeof(header));
      if (header.record_size < sizeof(SceneEnvironmentSystemRecordHeader)) {
        diagnostics.push_back(
          MakeErrorDiagnostic("scene.environment.record_size_invalid",
            "Environment system record size is invalid", source_id, {}));
        return outcome;
      }

      if (system.record_bytes.size()
        < static_cast<size_t>(header.record_size)) {
        diagnostics.push_back(
          MakeErrorDiagnostic("scene.environment.record_size_mismatch",
            "Environment system record size does not match payload", source_id,
            {}));
        return outcome;
      }

      env_header.byte_size += header.record_size;
      ++env_header.systems_count;
    }

    if (auto result = writer.WriteBlob(std::as_bytes(
          std::span<const SceneEnvironmentBlockHeader, 1>(&env_header, 1)));
      !result.has_value()) {
      diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
        "Failed to write environment header", source_id, {}));
      return outcome;
    }

    for (const auto& system : environment_systems) {
      SceneEnvironmentSystemRecordHeader header {};
      std::memcpy(&header, system.record_bytes.data(), sizeof(header));
      const auto record_size = static_cast<size_t>(header.record_size);
      if (auto result
        = writer.WriteBlob(std::span(system.record_bytes.data(), record_size));
        !result.has_value()) {
        diagnostics.push_back(MakeErrorDiagnostic("scene.serialize_failed",
          "Failed to write environment record", source_id, {}));
        return outcome;
      }
    }

    const auto bytes = stream.Data();
    outcome.bytes.assign(bytes.begin(), bytes.end());
    outcome.success = true;
    return outcome;
  }

  auto PatchContentHash(
    std::vector<std::byte>& bytes, const uint64_t content_hash) -> void
  {
    constexpr auto kOffset = offsetof(data::pak::AssetHeader, content_hash);
    if (bytes.size() < kOffset + sizeof(content_hash)) {
      return;
    }
    std::memcpy(bytes.data() + kOffset, &content_hash, sizeof(content_hash));
  }

} // namespace

ScenePipeline::ScenePipeline(
  co::ThreadPool& thread_pool, std::optional<Config> config)
  : thread_pool_(thread_pool)
  , config_(config.has_value() ? config.value() : Config {})
  , input_channel_(config_.queue_capacity)
  , output_channel_(config_.queue_capacity)
{
}

ScenePipeline::~ScenePipeline()
{
  if (started_) {
    DLOG_IF_F(
      WARNING, HasPending(), "Destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto ScenePipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "ScenePipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto ScenePipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto ScenePipeline::TrySubmit(WorkItem item) -> bool
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

auto ScenePipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .cooked = std::nullopt,
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

auto ScenePipeline::Close() -> void { input_channel_.Close(); }

auto ScenePipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0U;
}

auto ScenePipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto ScenePipeline::GetProgress() const noexcept -> PipelineProgress
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

auto ScenePipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

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

    const auto cook_start = std::chrono::steady_clock::now();
    std::vector<ImportDiagnostic> diagnostics;
    BuildOutcome outcome;

    if (item.adapter_owner == nullptr || item.build_stage == nullptr) {
      diagnostics.push_back(MakeErrorDiagnostic("scene.adapter_missing",
        "Scene adapter stage is missing", item.source_id, {}));
    } else {
      const SceneStageInput stage_input {
        .source_id = item.source_id,
        .geometry_keys = std::span<const AssetKey>(item.geometry_keys),
        .request = &item.request,
        .naming_service = item.naming_service,
        .stop_token = item.stop_token,
      };

      auto stage_outcome = co_await thread_pool_.Run(
        [adapter = item.adapter_owner, build_stage = item.build_stage,
          stage_input](
          co::ThreadPool::CancelToken canceled) -> StageRunOutcome {
          DLOG_F(1, "Build scene stage");
          StageRunOutcome out;
          if (canceled || stage_input.stop_token.stop_requested()) {
            out.canceled = true;
            return out;
          }
          out.result = build_stage(adapter.get(), stage_input, out.diagnostics);
          return out;
        });

      diagnostics.insert(diagnostics.end(), stage_outcome.diagnostics.begin(),
        stage_outcome.diagnostics.end());

      if (stage_outcome.canceled) {
        co_await ReportCancelled(std::move(item));
        continue;
      }

      if (stage_outcome.result.success) {
        DLOG_F(2, "Serialize scene on import thread source={}", item.source_id);
        SortSceneComponents(stage_outcome.result.build);

        const auto scene_name = item.request.GetSceneName();
        const auto virtual_path
          = item.request.loose_cooked_layout.SceneVirtualPath(scene_name);
        const auto scene_key = BuildSceneAssetKey(
          virtual_path, item.request.options.asset_key_policy);

        outcome
          = SerializeScene(scene_name, scene_key, stage_outcome.result.build,
            item.environment_systems, diagnostics, item.source_id);
      } else if (diagnostics.empty()) {
        diagnostics.push_back(MakeErrorDiagnostic("scene.stage_failed",
          "Scene adapter stage failed without diagnostics", item.source_id,
          {}));
      }
    }

    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    if (outcome.success && config_.with_content_hashing) {
      auto hash = co_await thread_pool_.Run(
        [bytes = std::span<const std::byte>(outcome.bytes),
          stop_token = item.stop_token](
          co::ThreadPool::CancelToken canceled) -> uint64_t {
          DLOG_F(1, "Compute content hash");
          if (stop_token.stop_requested() || canceled) {
            return uint64_t { 0 };
          }
          return util::ComputeContentHash(bytes);
        });

      if (hash != 0) {
        PatchContentHash(outcome.bytes, hash);
      }
    }

    WorkResult result {
      .source_id = std::move(item.source_id),
      .cooked = std::nullopt,
      .diagnostics = std::move(diagnostics),
      .telemetry = ImportWorkItemTelemetry {
        .cook_duration = MakeDuration(
          cook_start, std::chrono::steady_clock::now()),
      },
      .success = outcome.success,
    };

    if (outcome.success) {
      const auto scene_name = item.request.GetSceneName();
      const auto virtual_path
        = item.request.loose_cooked_layout.SceneVirtualPath(scene_name);
      const auto relpath
        = item.request.loose_cooked_layout.SceneDescriptorRelPath(scene_name);

      result.cooked = CookedScenePayload {
        .scene_key = BuildSceneAssetKey(
          virtual_path, item.request.options.asset_key_policy),
        .virtual_path = virtual_path,
        .descriptor_relpath = relpath,
        .descriptor_bytes = std::move(outcome.bytes),
      };
    }
    if (item.on_finished) {
      item.on_finished();
    }
    co_await output_channel_.Send(std::move(result));
  }
}

auto ScenePipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult canceled {
    .source_id = std::move(item.source_id),
    .cooked = std::nullopt,
    .diagnostics = { MakeCancelDiagnostic(item.source_id) },
    .success = false,
  };
  if (item.on_finished) {
    item.on_finished();
  }
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
