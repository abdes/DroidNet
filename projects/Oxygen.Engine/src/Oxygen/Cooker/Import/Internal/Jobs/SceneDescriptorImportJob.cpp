//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/TextureReferenceResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::detail {

namespace {

  namespace lc = oxygen::content::lc;

  struct MountedInspection final {
    std::filesystem::path root;
    std::optional<lc::Inspection> inspection;
  };

  struct SceneDescriptorExecutionContext final {
    ImportSession& session;
    const ImportRequest& request;
    std::vector<MountedInspection> mounts;
    std::unordered_map<std::string, std::pair<data::AssetKey, data::AssetType>>
      index_cache;
    std::unordered_map<std::string, std::unique_ptr<data::GeometryAsset>>
      geometries;
  };

  struct LinkedSceneDescriptor final {
    std::string scene_name;
    SceneBuild build;
    std::vector<data::AssetKey> geometry_keys;
    std::vector<SceneEnvironmentSystem> environment_systems;
  };

  class SceneDescriptorAdapter final {
  public:
    explicit SceneDescriptorAdapter(SceneBuild build)
      : build_(std::move(build))
    {
    }

    [[nodiscard]] auto BuildSceneStage(const SceneStageInput& stage_input,
      std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
    {
      if (stage_input.stop_token.stop_requested()) {
        diagnostics.push_back({
          .severity = ImportSeverity::kError,
          .code = "import.canceled",
          .message = "Import canceled",
          .source_path = std::string(stage_input.source_id),
          .object_path = {},
        });
        return {};
      }

      return SceneStageResult {
        .build = build_,
        .success = true,
      };
    }

  private:
    SceneBuild build_;
  };

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string object_path = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
  }

  auto ToLowerAscii(std::string value) -> std::string
  {
    for (auto& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  auto MakeDuration(const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  auto BuildNodeAssetKey(const ImportRequest& request,
    std::string_view scene_virtual_path, const uint32_t node_index)
    -> data::AssetKey
  {
    static_cast<void>(request);
    const auto key_path = std::string(scene_virtual_path) + "/nodes/"
      + std::to_string(node_index);
    return oxygen::data::AssetKey::FromVirtualPath(key_path);
  }

  auto InferAssetTypeFromRelPath(const std::string_view relpath)
    -> std::optional<data::AssetType>
  {
    const auto ext
      = ToLowerAscii(std::filesystem::path(relpath).extension().string());
    if (ext == LooseCookedLayout::kGeometryDescriptorExtension) {
      return data::AssetType::kGeometry;
    }
    if (ext == LooseCookedLayout::kMaterialDescriptorExtension) {
      return data::AssetType::kMaterial;
    }
    if (ext == LooseCookedLayout::kScriptDescriptorExtension) {
      return data::AssetType::kScript;
    }
    if (ext == LooseCookedLayout::kInputActionDescriptorExtension) {
      return data::AssetType::kInputAction;
    }
    if (ext == LooseCookedLayout::kInputMappingContextDescriptorExtension) {
      return data::AssetType::kInputMappingContext;
    }
    if (ext == LooseCookedLayout::kPhysicsSceneDescriptorExtension) {
      return data::AssetType::kPhysicsScene;
    }
    if (ext == LooseCookedLayout::kSceneDescriptorExtension) {
      return data::AssetType::kScene;
    }
    return std::nullopt;
  }

  auto LoadMountedInspections(SceneDescriptorExecutionContext& context) -> void
  {
    for (const auto& root :
      internal::BuildUniqueMountedCookedRoots(context.request)) {
      auto mount = MountedInspection {
        .root = root,
        .inspection = std::nullopt,
      };

      const auto index_path = mount.root / "container.index.bin";
      std::error_code ec;
      if (std::filesystem::exists(index_path, ec)) {
        try {
          auto inspection = lc::Inspection {};
          inspection.LoadFromRoot(mount.root);
          mount.inspection = std::move(inspection);
        } catch (const std::exception& ex) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "scene.descriptor.index_load_failed",
            "Failed loading cooked index: " + std::string(ex.what()),
            mount.root.string());
        }
      }

      context.mounts.push_back(std::move(mount));
    }
  }

  template <typename RecordT>
  auto PackRecordBytes(const RecordT& record) -> std::vector<std::byte>
  {
    const auto bytes = std::as_bytes(std::span<const RecordT, 1>(&record, 1));
    return { bytes.begin(), bytes.end() };
  }

  auto ResolveResourceDescriptorKey(SceneDescriptorExecutionContext& context,
    std::string_view virtual_path, std::string object_path)
    -> std::optional<data::AssetKey>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_invalid",
        "Reference virtual_path must be canonical", std::move(object_path));
      return std::nullopt;
    }

    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          context.request, virtual_path, relpath)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_unmounted",
        "Reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    auto file_matches = std::vector<std::filesystem::path> {};
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto candidate = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) {
        file_matches.push_back(candidate);
      }
    }

    if (file_matches.size() > 1U) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_ambiguous",
        "Reference virtual_path resolved to multiple mounted descriptors: "
          + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    if (file_matches.empty()) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_missing",
        "Reference virtual_path was not found: " + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

  auto ResolveAssetReference(SceneDescriptorExecutionContext& context,
    std::string_view virtual_path, std::optional<data::AssetType> expected_type,
    bool require_asset_key, std::string object_path)
    -> std::optional<std::pair<data::AssetKey, data::AssetType>>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_invalid",
        "Reference virtual_path must be canonical", std::move(object_path));
      return std::nullopt;
    }

    if (const auto it = context.index_cache.find(std::string(virtual_path));
      it != context.index_cache.end()) {
      const auto type = it->second.second;
      if (expected_type.has_value() && type != *expected_type) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.reference_type_mismatch",
          "Reference type mismatch; expected "
            + std::string(data::to_string(*expected_type)) + " but found "
            + std::string(data::to_string(type)),
          std::move(object_path));
        return std::nullopt;
      }
      return it->second;
    }

    auto relpath = std::string {};
    const auto is_mounted = internal::TryVirtualPathToRelPath(
      context.request, virtual_path, relpath);
    auto found_descriptor = false;
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (it->inspection.has_value()) {
        for (const auto& asset : it->inspection->Assets()) {
          if (asset.virtual_path != virtual_path) {
            continue;
          }
          const auto type = static_cast<data::AssetType>(asset.asset_type);
          if (expected_type.has_value() && type != *expected_type) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError,
              "scene.descriptor.reference_type_mismatch",
              "Reference type mismatch; expected "
                + std::string(data::to_string(*expected_type)) + " but found "
                + std::string(data::to_string(type)),
              std::move(object_path));
            return std::nullopt;
          }
          const auto resolved = std::make_pair(asset.key, type);
          context.index_cache.insert_or_assign(
            std::string(virtual_path), resolved);
          return resolved;
        }
      }
      if (is_mounted) {
        const auto candidate = it->root / std::filesystem::path(relpath);
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) {
          found_descriptor = true;
          break;
        }
      }
    }

    if (!is_mounted) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_unmounted",
        "Reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    if (!found_descriptor) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_missing",
        "Reference virtual_path was not found: " + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    const auto inferred_type = InferAssetTypeFromRelPath(relpath);
    if (!inferred_type.has_value()) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_unknown",
        "Unable to infer reference asset type from extension: "
          + std::string(virtual_path),
        std::move(object_path));
      return std::nullopt;
    }

    if (expected_type.has_value() && *inferred_type != *expected_type) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_mismatch",
        "Reference type mismatch; expected "
          + std::string(data::to_string(*expected_type)) + " but found "
          + std::string(data::to_string(*inferred_type)),
        std::move(object_path));
      return std::nullopt;
    }

    static_cast<void>(require_asset_key);

    const auto key = oxygen::data::AssetKey::FromVirtualPath(virtual_path);
    const auto resolved = std::make_pair(key, *inferred_type);
    context.index_cache.insert_or_assign(std::string(virtual_path), resolved);
    return resolved;
  }

  auto LoadSlotInventories(SceneDescriptorExecutionContext& context,
    const internal::SceneSource& source, IAsyncFileReader& file_reader)
    -> co::Co<bool>
  {
    for (const auto& renderable : source.renderables) {
      if (renderable.materials.empty()) {
        continue;
      }
      const auto& path = renderable.geometry;
      if (context.geometries.contains(path)) {
        continue;
      }
      const auto resolved = ResolveAssetReference(context, path,
        data::AssetType::kGeometry, true, "renderables.geometry_ref");
      if (!resolved) {
        co_return false;
      }
      std::filesystem::path descriptor_path;
      for (auto mount = context.mounts.rbegin(); mount != context.mounts.rend();
        ++mount) {
        if (mount->inspection) {
          for (const auto& asset : mount->inspection->Assets()) {
            if (asset.key == resolved->first && asset.virtual_path == path) {
              descriptor_path = mount->root / asset.descriptor_relpath;
              break;
            }
          }
        }
        if (!descriptor_path.empty()) {
          break;
        }
      }
      if (descriptor_path.empty()) {
        std::string relative;
        if (internal::TryVirtualPathToRelPath(
              context.request, path, relative)) {
          for (auto mount = context.mounts.rbegin();
            mount != context.mounts.rend(); ++mount) {
            const auto candidate = mount->root / relative;
            const auto exists = co_await file_reader.Exists(candidate);
            if (exists && exists.value()) {
              descriptor_path = candidate;
              break;
            }
          }
        }
      }
      const auto bytes = co_await file_reader.ReadFile(descriptor_path);
      if (!bytes) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.geometry_inventory_unavailable",
          "Cannot read geometry for slot validation", path);
        co_return false;
      }
      try {
        serio::ReadOnlyMemoryStream stream(bytes.value());
        serio::Reader reader(stream);
        const LoaderContext loader_context {
          .current_asset_key = resolved->first,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        };
        context.geometries.emplace(
          path, loaders::LoadGeometryAsset(loader_context));
      } catch (const std::exception& error) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.geometry_inventory_invalid", error.what(), path);
        co_return false;
      }
    }
    co_return true;
  }

  auto BuildPostProcessSystemRecord(SceneDescriptorExecutionContext& context,
    const internal::SceneSource::PostProcess& source,
    const data::pak::core::ResourceIndexT mask_index)
    -> std::optional<SceneEnvironmentSystem>
  {
    auto record = source.record;
    record.auto_exposure_metering_mask = mask_index;
    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    if (!serio::Store(writer, record)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.post_process_invalid",
        "Post-process values or record boundaries are invalid",
        "environment.post_process_volume");
      return std::nullopt;
    }
    for (const auto& key : source.curve) {
      if (!writer.Write(data::pak::world::ExposureCompensationKeyRecord {
            .metered_ev = key.metered_ev,
            .compensation_ev = key.compensation_ev,
          })) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.exposure_curve_write_failed",
          "Could not serialize exposure compensation curve",
          "environment.post_process_volume.auto_exposure_compensation_curve");
        return std::nullopt;
      }
    }
    const auto bytes = stream.Data();
    return SceneEnvironmentSystem {
      .system_type = static_cast<uint32_t>(
        data::pak::world::EnvironmentComponentType::kPostProcessVolume),
      .record_bytes = { bytes.begin(), bytes.end() },
    };
  }

  auto LinkSceneDescriptor(SceneDescriptorExecutionContext& context,
    internal::SceneSource source,
    const data::pak::core::ResourceIndexT mask_index)
    -> std::optional<LinkedSceneDescriptor>
  {
    auto prepared = LinkedSceneDescriptor {
      .scene_name = std::move(source.name),
      .build = std::move(source.build),
      .geometry_keys = {},
      .environment_systems = {},
    };
    const auto scene_virtual_path
      = context.request.loose_cooked_layout.SceneVirtualPath(
        prepared.scene_name);
    for (size_t i = 0; i < prepared.build.nodes.size(); ++i) {
      prepared.build.nodes.at(i).node_id = BuildNodeAssetKey(
        context.request, scene_virtual_path, static_cast<uint32_t>(i));
    }
    prepared.build.renderables.reserve(source.renderables.size());
    prepared.geometry_keys.reserve(source.renderables.size());
    for (size_t i = 0; i < source.renderables.size(); ++i) {
      const auto& source_renderable = source.renderables.at(i);
      const auto object_path = "renderables[" + std::to_string(i) + "]";
      const auto resolved_geometry
        = ResolveAssetReference(context, source_renderable.geometry,
          data::AssetType::kGeometry, true, object_path + ".geometry_ref");
      if (!resolved_geometry.has_value()) {
        return std::nullopt;
      }
      if (!source_renderable.materials.empty()) {
        const auto& geometry
          = *context.geometries.at(source_renderable.geometry);
        for (const auto& assignment : source_renderable.materials) {
          if (geometry.FindMaterialSlot(assignment.slot_id) == nullptr) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError, "scene.descriptor.material_slot_invalid",
              "Material assignment targets a missing slot",
              object_path + ".material_overrides");
            return std::nullopt;
          }
          const auto material = ResolveAssetReference(context,
            assignment.material, data::AssetType::kMaterial, true,
            object_path + ".material_overrides");
          if (!material.has_value()) {
            return std::nullopt;
          }
          prepared.build.material_overrides.push_back({
            .node_index = source_renderable.node_index,
            .slot_id = assignment.slot_id,
            .material_key = material->first,
            .layout_revision = geometry.MaterialSlots().layout_revision,
          });
        }
      }
      auto renderable = data::pak::world::RenderableRecord {};
      renderable.node_index = source_renderable.node_index;
      renderable.geometry_key = resolved_geometry->first;
      renderable.visible = source_renderable.visible ? 1U : 0U;
      prepared.build.renderables.push_back(renderable);
      prepared.geometry_keys.push_back(renderable.geometry_key);
    }

    if (source.post_process.has_value()) {
      auto system = BuildPostProcessSystemRecord(
        context, *source.post_process, mask_index);
      if (!system.has_value()) {
        return std::nullopt;
      }
      prepared.environment_systems.push_back(std::move(*system));
    }
    if (source.background.has_value()) {
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kBackground),
        .record_bytes = PackRecordBytes(*source.background),
      });
    }
    if (source.atmosphere.has_value()) {
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kSkyAtmosphere),
        .record_bytes = PackRecordBytes(*source.atmosphere),
      });
    }
    if (source.fog.has_value()) {
      auto& fog = *source.fog;
      if (fog.cubemap.has_value()) {
        const auto key = ResolveResourceDescriptorKey(context, *fog.cubemap,
          "environment.fog.inscattering_color_cubemap_ref");
        if (!key.has_value()) {
          return std::nullopt;
        }
        fog.record.inscattering_color_cubemap_asset = *key;
      }
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kFog),
        .record_bytes = PackRecordBytes(fog.record),
      });
    }
    if (source.sky_light.has_value()) {
      auto& sky = *source.sky_light;
      if (sky.cubemap.has_value()) {
        const auto key = ResolveResourceDescriptorKey(
          context, *sky.cubemap, "environment.sky_light.cubemap_ref");
        if (!key.has_value()) {
          return std::nullopt;
        }
        sky.record.cubemap_asset = *key;
      }
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kSkyLight),
        .record_bytes = PackRecordBytes(sky.record),
      });
    }
    for (const auto& reference : source.references) {
      if (!ResolveAssetReference(context, reference.virtual_path,
            reference.type, false, reference.object_path)
            .has_value()) {
        return std::nullopt;
      }
    }
    return prepared;
  }

} // namespace

auto SceneDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting scene descriptor job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    const auto finalize_start = std::chrono::steady_clock::now();
    auto report = co_await FinalizeSession(session);
    const auto finalize_end = std::chrono::steady_clock::now();
    telemetry.finalize_duration = MakeDuration(finalize_start, finalize_end);
    telemetry.total_duration = MakeDuration(job_start, finalize_end);
    telemetry.io_duration = session.IoDuration();
    telemetry.source_load_duration = session.SourceLoadDuration();
    telemetry.decode_duration = session.DecodeDuration();
    telemetry.load_duration
      = session.SourceLoadDuration() + session.LoadDuration();
    telemetry.cook_duration = session.CookDuration();
    telemetry.emit_duration = session.EmitDuration();
    report.telemetry = telemetry;
    co_return report;
  };

  EnsureCookedRoot();
  auto& session = Session();

  const auto& descriptor = Request().scene_descriptor;
  if (!descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "scene.descriptor.request_invalid",
      "SceneDescriptorImportJob requires request.scene_descriptor payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid scene descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto source = internal::SceneSource::FromDescriptor(
    descriptor->normalized_descriptor_json, Request().source_path, diagnostics);
  AddDiagnostics(session, std::move(diagnostics));
  if (!source.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor preparation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = SceneDescriptorExecutionContext {
    .session = session,
    .request = Request(),
    .mounts = {},
    .index_cache = {},
    .geometries = {},
  };
  LoadMountedInspections(context);

  if (!CookedReader()
    || !co_await LoadSlotInventories(context, *source, *CookedReader())) {
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto mask_index = data::pak::core::kNoResourceIndex;
  if (source->post_process.has_value()
    && source->post_process->metering_mask.has_value()) {
    const auto& path = *source->post_process->metering_mask;
    const auto reader = CookedReader();
    if (!reader) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "scene.descriptor.mask_reader_unavailable",
        "Texture descriptor reader is unavailable",
        "environment.post_process_volume.auto_exposure_metering_mask");
      co_return co_await FinalizeWithTelemetry(session);
    }
    const auto reference = co_await internal::ResolveTextureReference(
      observer_ptr { &session }, observer_ptr { &Request() }, reader,
      {
        .virtual_path = path,
        .object_path
        = "environment.post_process_volume.auto_exposure_metering_mask",
        .diagnostic_prefix = "scene.descriptor.",
      });
    if (!reference) {
      co_return co_await FinalizeWithTelemetry(session);
    }
    auto source_error = std::error_code {};
    const bool local_source = std::filesystem::equivalent(
      reference->cooked_root, session.CookedRoot(), source_error);
    if (source_error || !local_source
      || reference->index == data::pak::core::kNoResourceIndex) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "scene.descriptor.mask_source_invalid",
        "Metering mask must reference a texture cooked into the scene's own "
        "source",
        "environment.post_process_volume.auto_exposure_metering_mask");
      co_return co_await FinalizeWithTelemetry(session);
    }
    const auto& desc = reference->descriptor;
    const auto format = static_cast<Format>(desc.format);
    if (format == Format::kUnknown || format > Format::kMaxFormat
      || desc.texture_type != static_cast<uint8_t>(TextureType::kTexture2D)
      || desc.width == 0U || desc.height == 0U || desc.depth != 1U
      || desc.array_layers != 1U) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "scene.descriptor.mask_format_invalid",
        "Metering mask requires a valid linear 2D color texture",
        "environment.post_process_volume.auto_exposure_metering_mask");
      co_return co_await FinalizeWithTelemetry(session);
    }
    const auto& info = graphics::detail::GetFormatInfo(format);
    if (info.is_srgb || info.has_depth || info.has_stencil || !info.has_red
      || info.kind == graphics::detail::FormatKind::kInteger) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "scene.descriptor.mask_format_invalid",
        "Metering mask requires a linear non-integer color format with a red "
        "channel",
        "environment.post_process_volume.auto_exposure_metering_mask");
      co_return co_await FinalizeWithTelemetry(session);
    }
    mask_index = reference->index;
  }
  auto prepared_opt
    = LinkSceneDescriptor(context, std::move(*source), mask_index);
  if (!prepared_opt.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (session.HasErrors()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto& prepared = *prepared_opt;
  auto request_for_pipeline = Request();
  request_for_pipeline.source_path = std::filesystem::path(prepared.scene_name);
  auto& pipeline = CreatePipeline<ScenePipeline>(*ThreadPool(),
    ScenePipeline::Config {
      .queue_capacity = Concurrency().scene.queue_capacity,
      .worker_count = Concurrency().scene.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });

  auto adapter
    = std::make_shared<SceneDescriptorAdapter>(std::move(prepared.build));
  auto item = ScenePipeline::WorkItem::MakeWorkItem(std::move(adapter),
    Request().source_path.string(), prepared.geometry_keys,
    prepared.environment_systems, std::move(request_for_pipeline),
    observer_ptr { &GetNamingService() }, StopToken());

  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  if (result.telemetry.cook_duration.has_value()) {
    session.AddCookDuration(*result.telemetry.cook_duration);
  }
  if (result.telemetry.load_duration.has_value()) {
    session.AddLoadDuration(*result.telemetry.load_duration);
  }
  if (result.telemetry.io_duration.has_value()) {
    session.AddIoDuration(*result.telemetry.io_duration);
  }
  AddDiagnostics(session, std::move(result.diagnostics));

  if (!result.success || !result.cooked.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  const auto& cooked = *result.cooked;
  session.AssetEmitter().Emit(cooked.scene_key, data::AssetType::kScene,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes);
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto SceneDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
