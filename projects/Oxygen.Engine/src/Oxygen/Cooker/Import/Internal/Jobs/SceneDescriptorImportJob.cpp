//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
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
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/SceneDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/TextureReferenceResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioWriters.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::detail {

namespace {

  namespace lc = oxygen::content::lc;

  struct SceneDescriptorExecutionContext final {
    // NOLINTBEGIN(*-avoid-const-or-ref-data-members)
    ImportSession& session;
    const ImportRequest& request;
    // NOLINTEND(*-avoid-const-or-ref-data-members)
    std::vector<internal::MountedCookedSource> mounts;
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
    data::AssetReferences environment_references;
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

  auto MakeDuration(const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  template <typename RecordT>
  auto PackRecordBytes(const RecordT& record) -> std::vector<std::byte>
  {
    const auto bytes = std::as_bytes(std::span<const RecordT, 1>(&record, 1));
    return { bytes.begin(), bytes.end() };
  }

  auto ResolveAssetReference(SceneDescriptorExecutionContext& context,
    std::string_view virtual_path, std::optional<data::AssetType> expected_type,
    std::string object_path)
    -> std::optional<std::pair<data::AssetKey, data::AssetType>>
  {
    const auto type_mismatch = [&](const data::AssetType found) -> void {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_mismatch",
        "Reference type mismatch; expected "
          + std::string(data::to_string(*expected_type)) + " but found "
          + std::string(data::to_string(found)),
        std::move(object_path));
    };
    if (const auto it = context.index_cache.find(std::string(virtual_path));
      it != context.index_cache.end()) {
      if (expected_type.has_value() && it->second.second != *expected_type) {
        type_mismatch(it->second.second);
        return std::nullopt;
      }
      return it->second;
    }

    const auto resolved = internal::ResolveMountedAssetReference(
      context.mounts, context.request, virtual_path, expected_type);
    if (resolved.has_value()) {
      const auto entry = std::make_pair(resolved->key, resolved->type);
      context.index_cache.insert_or_assign(std::string(virtual_path), entry);
      return entry;
    }
    switch (resolved.error().reason) {
    case internal::AssetReferenceRefusal::kInvalidVirtualPath:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_invalid",
        "Reference virtual_path must be canonical", std::move(object_path));
      break;
    case internal::AssetReferenceRefusal::kUnmounted:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_virtual_path_unmounted",
        "Reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      break;
    case internal::AssetReferenceRefusal::kMissing:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_missing",
        "Reference virtual_path was not found: " + std::string(virtual_path),
        std::move(object_path));
      break;
    case internal::AssetReferenceRefusal::kTypeUnknown:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "scene.descriptor.reference_type_unknown",
        "Unable to infer reference asset type from extension: "
          + std::string(virtual_path),
        std::move(object_path));
      break;
    case internal::AssetReferenceRefusal::kTypeMismatch:
      if (resolved.error().found_type.has_value()) {
        type_mismatch(*resolved.error().found_type);
      }
      break;
    }
    return std::nullopt;
  }

  //! A cooked descriptor looked up in the mounted sources' listings.
  struct ListedDescriptor final {
    //! Whether a mounted source lists the asset.
    bool listed = false;
    //! The descriptor bytes; empty when a listed descriptor cannot be read.
    std::optional<std::vector<std::byte>> bytes;
  };

  //! Reads the descriptor of an asset a mounted source lists, newest first.
  auto ReadListedDescriptor(const SceneDescriptorExecutionContext& context,
    const data::AssetKey& key, IAsyncFileReader& file_reader)
    -> co::Co<ListedDescriptor>
  {
    for (auto mount = context.mounts.rbegin(); mount != context.mounts.rend();
      ++mount) {
      if (!mount->inspection) {
        continue;
      }
      const auto assets = mount->inspection->Assets();
      const auto asset = std::ranges::find(assets, key, &lc::AssetEntry::key);
      if (asset == assets.end()) {
        continue;
      }
      if (mount->inspection->IsPak()) {
        try {
          co_return ListedDescriptor {
            .listed = true,
            .bytes = mount->inspection->ReadAssetDescriptor(key),
          };
        } catch (const std::exception&) {
          co_return ListedDescriptor { .listed = true, .bytes = std::nullopt };
        }
      }
      auto read = co_await file_reader.ReadFile(
        mount->root / std::filesystem::path(asset->descriptor_relpath));
      if (!read) {
        co_return ListedDescriptor { .listed = true, .bytes = std::nullopt };
      }
      co_return ListedDescriptor {
        .listed = true,
        .bytes = std::move(read.value()),
      };
    }
    co_return ListedDescriptor {};
  }

  auto LoadSurfaceInventories(SceneDescriptorExecutionContext& context,
    const internal::SceneSource& source, IAsyncFileReader& file_reader)
    -> co::Co<bool>
  {
    for (const auto& renderable : source.renderables) {
      const auto names_a_surface = std::ranges::any_of(renderable.overrides,
        [](const internal::SceneSource::Override& entry) -> bool {
          return entry.property->part_kind == data::schema::PartKind::kSurface;
        });
      if (!names_a_surface) {
        continue;
      }
      const auto& path = renderable.geometry;
      if (context.geometries.contains(path)) {
        continue;
      }
      const auto resolved = ResolveAssetReference(
        context, path, data::AssetType::kGeometry, "renderables.geometry_ref");
      if (!resolved) {
        co_return false;
      }
      auto bytes
        = (co_await ReadListedDescriptor(context, resolved->first, file_reader))
            .bytes;
      if (!bytes) {
        // A descriptor this cook writes is not listed in an index yet.
        std::string relative;
        if (internal::TryVirtualPathToRelPath(
              context.request, path, relative)) {
          for (auto mount = context.mounts.rbegin();
            mount != context.mounts.rend() && !bytes; ++mount) {
            const auto candidate = mount->root / relative;
            const auto exists = co_await file_reader.Exists(candidate);
            if (exists && exists.value()) {
              if (auto read = co_await file_reader.ReadFile(candidate)) {
                bytes = std::move(read.value());
              }
            }
          }
        }
      }
      if (!bytes) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.geometry_inventory_unavailable",
          "Cannot read geometry for surface validation", path);
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

  //! What an environment record uses a texture for.
  enum class EnvironmentTextureUse : uint8_t {
    kMeteringMask,
    kCubemap,
  };

  //! Textures resolved for the scene's environment records: the scene's own
  //! texture-table entries or other sources' textures by key.
  struct EnvironmentTextureIndices final {
    data::TextureTarget metering_mask;
    data::TextureTarget fog_cubemap;
    data::TextureTarget sky_light_cubemap;
    data::TextureTarget sky_sphere_cubemap;
  };

  auto EnvironmentTextureDiagnosticPrefix(const EnvironmentTextureUse use)
    -> std::string_view
  {
    return use == EnvironmentTextureUse::kCubemap ? "scene.descriptor.cubemap_"
                                                  : "scene.descriptor.mask_";
  }

  //! Returns why `desc` cannot serve `use`, or nothing when it can.
  auto EnvironmentTextureFormatError(
    const data::pak::core::TextureResourceDesc& desc,
    const EnvironmentTextureUse use) -> std::optional<std::string_view>
  {
    const auto format = static_cast<Format>(desc.format);
    const bool known_format
      = format != Format::kUnknown && format <= Format::kMaxFormat;
    if (use == EnvironmentTextureUse::kCubemap) {
      if (!known_format
        || desc.texture_type != static_cast<uint8_t>(TextureType::kTextureCube)
        || desc.width == 0U || desc.width != desc.height || desc.depth != 1U
        || desc.array_layers != kCubeFaceCount) {
        return "Sky cubemap requires a cube texture with six square faces";
      }
      return std::nullopt;
    }
    if (!known_format
      || desc.texture_type != static_cast<uint8_t>(TextureType::kTexture2D)
      || desc.width == 0U || desc.height == 0U || desc.depth != 1U
      || desc.array_layers != 1U) {
      return "Metering mask requires a valid linear 2D color texture";
    }
    const auto& info = graphics::detail::GetFormatInfo(format);
    if (info.is_srgb || info.has_depth || info.has_stencil || !info.has_red
      || info.kind == graphics::detail::FormatKind::kInteger) {
      return "Metering mask requires a linear non-integer color format with a "
             "red channel";
    }
    return std::nullopt;
  }

  //! Resolves a texture an environment record binds. A deleted texture leaves
  //! the scene without it until fixed; other failures report an error and
  //! return nothing.
  auto ResolveEnvironmentTexture(observer_ptr<ImportSession> session,
    observer_ptr<const ImportRequest> request,
    observer_ptr<IAsyncFileReader> reader,
    const std::span<const internal::MountedCookedSource> mounts,
    std::string virtual_path, std::string object_path,
    const EnvironmentTextureUse use)
    -> co::Co<std::optional<data::TextureTarget>>
  {
    const auto prefix = std::string(EnvironmentTextureDiagnosticPrefix(use));
    if (!reader) {
      AddDiagnostic(*session, *request, ImportSeverity::kError,
        prefix + "reader_unavailable",
        "Texture descriptor reader is unavailable", object_path);
      co_return std::nullopt;
    }
    const auto reference = co_await internal::ResolveTextureReference(session,
      request, reader, mounts,
      {
        .virtual_path = std::move(virtual_path),
        .object_path = object_path,
        .diagnostic_prefix = "scene.descriptor.",
        .allow_missing = true,
      });
    if (!reference) {
      co_return std::nullopt;
    }
    if (reference->missing) {
      co_return data::TextureTarget {};
    }
    if (const auto error
      = EnvironmentTextureFormatError(reference->descriptor, use)) {
      AddDiagnostic(*session, *request, ImportSeverity::kError,
        prefix + "format_invalid", std::string(*error), object_path);
      co_return std::nullopt;
    }
    co_return reference->target;
  }

  auto BuildPostProcessSystemRecord(SceneDescriptorExecutionContext& context,
    const internal::SceneSource::PostProcess& source,
    const data::ResourceReferenceIndex mask_index)
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

  //! The cooked geometry `key` names, parsed for its surfaces; null when it
  //! is not cooked or cannot be parsed.
  auto ReadGeometry(const SceneDescriptorExecutionContext& context,
    const data::AssetKey& key, IAsyncFileReader& file_reader)
    -> co::Co<std::shared_ptr<const data::GeometryAsset>>
  {
    auto bytes
      = (co_await ReadListedDescriptor(context, key, file_reader)).bytes;
    if (!bytes) {
      co_return nullptr;
    }
    try {
      serio::ReadOnlyMemoryStream stream(bytes.value());
      serio::Reader reader(stream);
      const LoaderContext loader_context {
        .current_asset_key = key,
        .desc_reader = &reader,
        .work_offline = true,
        .parse_only = true,
      };
      co_return loaders::LoadGeometryAsset(loader_context);
    } catch (const std::exception&) {
      co_return nullptr;
    }
  }

  using SceneMap = std::unordered_map<data::AssetKey,
    std::shared_ptr<const data::SceneAsset>>;

  //! An instance of the scene being cooked: its node, cooked key and source.
  struct CookedInstance final {
    data::AssetKey key;
    observer_ptr<internal::SceneSource::Instance> source;
  };

  //! Reads the cooked scenes reachable through `instances`. A scene not
  //! cooked yet is absent: its own cook checks it.
  auto ReadInstancedScenes(SceneDescriptorExecutionContext& context,
    std::span<const CookedInstance> instances, IAsyncFileReader& file_reader)
    -> co::Co<std::optional<SceneMap>>
  {
    struct Pending final {
      data::AssetKey key;
      std::string_view object_path;
    };
    auto pending = std::vector<Pending> {};
    for (const auto& instance : instances) {
      pending.push_back(
        { .key = instance.key, .object_path = instance.source->object_path });
    }
    auto scenes = SceneMap {};
    auto visited = std::unordered_set<data::AssetKey> {};
    while (!pending.empty()) {
      const auto [key, object_path] = pending.back();
      pending.pop_back();
      if (!visited.insert(key).second) {
        continue;
      }
      auto listed = co_await ReadListedDescriptor(context, key, file_reader);
      if (!listed.listed) {
        continue;
      }
      if (!listed.bytes) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.instance_unreadable",
          "Cannot read an instanced scene: " + data::to_string(key),
          std::string(object_path));
        co_return std::nullopt;
      }
      try {
        auto scene = std::make_shared<const data::SceneAsset>(
          key, std::move(*listed.bytes));
        for (const auto& nested :
          scene->GetComponents<data::pak::world::SceneInstanceRecord>()) {
          pending.push_back(
            { .key = nested.scene_key, .object_path = object_path });
        }
        scenes.emplace(key, std::move(scene));
      } catch (const std::exception& error) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.instance_invalid",
          "An instanced scene is invalid; re-cook it: "
            + std::string(error.what()),
          std::string(object_path));
        co_return std::nullopt;
      }
    }
    co_return scenes;
  }

  //! The change a descriptor instance makes, as composition reads it. Only
  //! a value a node's records hold is needed: asset-sourced values are
  //! linked later and resolve by node and part alone.
  auto ToSceneChange(const internal::SceneSource::InstanceChange& source)
    -> data::SceneChange
  {
    auto record = data::pak::world::OverrideRecord {
      .override_id = source.change.property->id,
      .part_id = source.change.part_id,
      .layout_revision = source.change.layout_revision,
    };
    if (const auto* encoded
      = std::get_if<data::pak::world::OverrideValue>(&source.change.value)) {
      record.value = *encoded;
    } else if (const auto* flag = std::get_if<bool>(&source.change.value)) {
      record.value.as_bool = *flag ? 1U : 0U;
    }
    return { .path = source.path, .change = record };
  }

  //! The composed node `path` names below the instance root, if any.
  auto FindComposedNode(const data::ComposedScene& composed,
    std::span<const data::NodeId> path) -> const data::ComposedNode*
  {
    for (const auto& occurrence : composed.occurrences) {
      for (const auto& node : occurrence.nodes) {
        if (std::ranges::equal(node.path, path)) {
          return &node;
        }
      }
    }
    return nullptr;
  }

  //! Composes each instance of the scene with the cooked instanced scenes.
  /*!
   A cycle, or a change whose value its record refuses, fails the cook. A
   change that does not resolve, or names a surface its geometry does not
   declare, is a warning and left out; so is a starting camera below an
   instance root that names no camera. An instance of a scene not cooked yet
   is not checked.
  */
  auto ComposeInstances(SceneDescriptorExecutionContext& context,
    const data::AssetKey& scene_key, internal::SceneSource& source,
    IAsyncFileReader& file_reader) -> co::Co<bool>
  {
    auto instances = std::vector<CookedInstance> {};
    for (auto& instance : source.instances) {
      const auto resolved = ResolveAssetReference(
        context, instance.scene, data::AssetType::kScene, instance.object_path);
      if (!resolved.has_value()) {
        co_return false;
      }
      // Linking refuses a scene that instances itself.
      if (resolved->first != scene_key) {
        instances.push_back(
          { .key = resolved->first, .source = observer_ptr { &instance } });
      }
    }
    auto scenes = co_await ReadInstancedScenes(context, instances, file_reader);
    if (!scenes.has_value()) {
      co_return false;
    }
    const auto resolve =
      [&scenes](
        const data::AssetKey& key) -> std::shared_ptr<const data::SceneAsset> {
      const auto found = scenes->find(key);
      return found != scenes->end() ? found->second : nullptr;
    };
    auto geometries = std::unordered_map<data::AssetKey,
      std::shared_ptr<const data::GeometryAsset>> {};
    const auto ancestors = std::array { scene_key };
    auto valid = true;
    for (const auto& [key, instance] : instances) {
      auto changes = std::vector<data::SceneChange> {};
      changes.reserve(instance->changes.size());
      for (const auto& change : instance->changes) {
        changes.push_back(ToSceneChange(change));
      }
      const auto composed = data::ComposeInstance(
        {
          .source = key,
          .instance_root = instance->node_index,
          .changes = changes,
          .ancestors = ancestors,
        },
        resolve);
      for (const auto& issue : composed.issues) {
        if (issue.code == data::CompositionIssueCode::kSceneMissing) {
          continue;
        }
        const auto cycle = issue.code == data::CompositionIssueCode::kCycle;
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          cycle ? "scene.descriptor.instance_cycle"
                : "scene.descriptor.instance_invalid",
          cycle ? "An instanced scene instances a scene on its own path"
                : "An instanced scene cannot be composed: "
              + std::string(data::to_string(issue.code)),
          instance->object_path);
        valid = false;
      }
      if (composed.occurrences.empty()) {
        continue;
      }

      // Left out: changes that do not resolve, and surface changes whose
      // geometry does not declare the surface.
      auto kept = std::vector<internal::SceneSource::InstanceChange> {};
      for (auto&& [index, change] : std::views::enumerate(instance->changes)) {
        const auto& as_composed = changes.at(static_cast<size_t>(index));
        const auto unresolved = std::ranges::any_of(composed.unresolved,
          [&](const data::UnresolvedChange& entry) -> bool {
            const auto* record = std::get_if<data::pak::world::OverrideRecord>(
              &entry.change.change);
            const auto& own
              = std::get<data::pak::world::OverrideRecord>(as_composed.change);
            return record != nullptr && entry.change.path == as_composed.path
              && record->override_id == own.override_id
              && record->part_id == own.part_id;
          });
        if (unresolved) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kWarning,
            "scene.descriptor.instance_override_unresolved",
            "The change names no node of the instanced scene, or a property "
            "that node does not have; it has no effect until it resolves",
            change.change.object_path);
          continue;
        }
        if (change.change.property->part_kind
          == data::schema::PartKind::kSurface) {
          const auto* node = FindComposedNode(composed, change.path);
          if (node != nullptr && node->renderable.has_value()) {
            const auto geometry_key = node->renderable->geometry_key;
            if (!geometries.contains(geometry_key)) {
              geometries.emplace(geometry_key,
                co_await ReadGeometry(context, geometry_key, file_reader));
            }
            const auto& geometry = geometries.at(geometry_key);
            if (geometry
              && geometry->FindSurface(change.change.part_id) == nullptr) {
              AddDiagnostic(context.session, context.request,
                ImportSeverity::kWarning, "scene.descriptor.surface_invalid",
                "The change targets a surface the geometry does not declare; "
                "it has no effect until the geometry declares the surface "
                "again",
                change.change.object_path);
              continue;
            }
          }
        }
        kept.push_back(std::move(change));
      }
      instance->changes = std::move(kept);

      auto& camera = source.build.starting_camera;
      if (camera.node_index == instance->node_index
        && !source.starting_camera_path.empty()) {
        const auto* node
          = FindComposedNode(composed, source.starting_camera_path);
        if (node == nullptr
          || (!node->perspective_camera.has_value()
            && !node->orthographic_camera.has_value())) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kWarning, "scene.starting_camera.unresolved",
            "The starting camera names no camera of the instanced scene; the "
            "scene starts through its first camera",
            "starting_camera");
          camera = { .node_index = data::pak::world::kNoStartingCamera };
          source.starting_camera_path.clear();
        }
      }
    }
    co_return valid;
  }

  //! Encodes the value of one override as its property's value type,
  //! resolving an asset against the mounts.
  auto LinkOverrideValue(SceneDescriptorExecutionContext& context,
    const internal::SceneSource::Override& source,
    data::pak::world::OverrideRecord& record, SceneBuild& build) -> bool
  {
    const auto& property = *source.property;
    // SceneSource read each value as its property's value type, and encoded
    // the change of every property a node's records hold.
    if (const auto* encoded
      = std::get_if<data::pak::world::OverrideValue>(&source.value)) {
      record.value = *encoded;
    } else if (const auto* flag = std::get_if<bool>(&source.value)) {
      DCHECK_F(property.type == data::schema::ValueType::kBool);
      record.value.as_bool = *flag ? 1U : 0U;
    } else if (const auto* asset
      = std::get_if<internal::SceneSource::AssetReference>(&source.value)) {
      DCHECK_F(property.type == data::schema::ValueType::kAsset);
      const auto resolved = ResolveAssetReference(
        context, asset->virtual_path, property.asset_type, source.object_path);
      if (!resolved.has_value()) {
        return false;
      }
      record.value.as_asset = resolved->first;
    } else {
      DCHECK_F(property.type == data::schema::ValueType::kLodPolicy);
      record.value.as_record_index = data::pak::world::OverrideRecordIndex {
        static_cast<uint32_t>(build.override_lod_policies.size()),
      };
      build.override_lod_policies.push_back(
        std::get<data::pak::geometry::LodPolicyRecord>(source.value));
    }
    build.overrides.push_back(record);
    return true;
  }

  //! Links one override of a renderable of this scene. A surface the geometry
  //! no longer declares keeps the override in the descriptor and leaves it out
  //! of the cooked scene, with a warning.
  auto LinkOverride(SceneDescriptorExecutionContext& context,
    const internal::SceneSource::Renderable& renderable,
    const internal::SceneSource::Override& source, SceneBuild& build) -> bool
  {
    const auto& property = *source.property;
    auto record = data::pak::world::OverrideRecord {
      .target = { .node_index = renderable.node_index },
      .override_id = property.id,
    };
    switch (property.part_kind) {
    case data::schema::PartKind::kNone:
      DCHECK_F(source.part_id.IsNil());
      break;
    case data::schema::PartKind::kSurface: {
      const auto& geometry = *context.geometries.at(renderable.geometry);
      if (geometry.FindSurface(source.part_id) == nullptr) {
        AddDiagnostic(context.session, context.request,
          ImportSeverity::kWarning, "scene.descriptor.surface_invalid",
          "The override targets a surface the geometry does not declare; it "
          "has no effect until the geometry declares the surface again",
          source.object_path);
        return true;
      }
      record.part_id = source.part_id;
      record.layout_revision = geometry.Surfaces().layout_revision;
      break;
    }
    }
    return LinkOverrideValue(context, source, record, build);
  }

  //! Links one change of a node an instance brings in. It keeps its authored
  //! surface and layout revision: the instanced scene owns the geometry.
  auto LinkInstanceChange(SceneDescriptorExecutionContext& context,
    const uint32_t instance_root,
    const internal::SceneSource::InstanceChange& change, SceneBuild& build)
    -> bool
  {
    auto record = data::pak::world::OverrideRecord {
      .target = {
        .node_index = instance_root,
        .node_path = build.node_paths.Intern(change.path),
      },
      .override_id = change.change.property->id,
      .part_id = change.change.part_id,
      .layout_revision = change.change.layout_revision,
    };
    return LinkOverrideValue(context, change.change, record, build);
  }

  auto LinkSceneDescriptor(SceneDescriptorExecutionContext& context,
    internal::SceneSource source, const EnvironmentTextureIndices& textures)
    -> std::optional<LinkedSceneDescriptor>
  {
    auto prepared = LinkedSceneDescriptor {
      .scene_name = std::move(source.name),
      .build = std::move(source.build),
      .geometry_keys = {},
      .environment_systems = {},
      .environment_references = {},
    };
    // The job placed the scene at its identity before linking it.
    const auto& scene_virtual_path = context.request.asset_virtual_path;
    if (!source.starting_camera_path.empty()) {
      prepared.build.starting_camera.node_path
        = prepared.build.node_paths.Intern(source.starting_camera_path);
    }
    prepared.build.renderables.reserve(source.renderables.size());
    prepared.geometry_keys.reserve(source.renderables.size());
    for (size_t i = 0; i < source.renderables.size(); ++i) {
      const auto& source_renderable = source.renderables.at(i);
      const auto object_path = "renderables[" + std::to_string(i) + "]";
      const auto resolved_geometry
        = ResolveAssetReference(context, source_renderable.geometry,
          data::AssetType::kGeometry, object_path + ".geometry_ref");
      if (!resolved_geometry.has_value()) {
        return std::nullopt;
      }
      for (const auto& source_override : source_renderable.overrides) {
        if (!LinkOverride(
              context, source_renderable, source_override, prepared.build)) {
          return std::nullopt;
        }
      }
      auto renderable = data::pak::world::RenderableRecord {};
      renderable.node_index = source_renderable.node_index;
      renderable.geometry_key = resolved_geometry->first;
      prepared.build.renderables.push_back(renderable);
      prepared.geometry_keys.push_back(renderable.geometry_key);
    }

    const auto scene_key = data::AssetKey::FromVirtualPath(scene_virtual_path);
    prepared.build.instances.reserve(source.instances.size());
    for (auto& instance : source.instances) {
      const auto resolved = ResolveAssetReference(
        context, instance.scene, data::AssetType::kScene, instance.object_path);
      if (!resolved.has_value()) {
        return std::nullopt;
      }
      if (resolved->first == scene_key) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.instance_cycle", "A scene cannot instance itself",
          std::move(instance.object_path));
        return std::nullopt;
      }
      for (const auto& change : instance.changes) {
        if (!LinkInstanceChange(
              context, instance.node_index, change, prepared.build)) {
          return std::nullopt;
        }
      }
      prepared.build.instances.push_back({
        .node_index = instance.node_index,
        .scene_key = resolved->first,
      });
    }
    auto starting_contexts = std::unordered_set<data::AssetKey> {};
    for (auto& starting : source.starting_input_contexts) {
      const auto resolved = ResolveAssetReference(context, starting.context,
        data::AssetType::kInputMappingContext, starting.object_path);
      if (!resolved.has_value()) {
        return std::nullopt;
      }
      if (!starting_contexts.insert(resolved->first).second) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "scene.descriptor.starting_input_context_duplicate",
          "An input context can start only once",
          std::move(starting.object_path));
        return std::nullopt;
      }
      prepared.build.starting_input_contexts.push_back({
        .context_key = resolved->first,
        .priority = starting.priority,
      });
    }

    AssetReferenceBuilder references;
    const auto bind_texture = [&references](const data::TextureTarget& target)
      -> data::ResourceReferenceIndex { return references.AddTexture(target); };
    if (source.post_process.has_value()) {
      auto system = BuildPostProcessSystemRecord(
        context, *source.post_process, bind_texture(textures.metering_mask));
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
      auto& fog = source.fog->record;
      fog.inscattering_color_cubemap = bind_texture(textures.fog_cubemap);
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kFog),
        .record_bytes = PackRecordBytes(fog),
      });
    }
    if (source.sky_light.has_value()) {
      auto& sky = source.sky_light->record;
      sky.cubemap = bind_texture(textures.sky_light_cubemap);
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kSkyLight),
        .record_bytes = PackRecordBytes(sky),
      });
    }
    if (source.sky_sphere.has_value()) {
      auto& sphere = source.sky_sphere->record;
      sphere.cubemap = bind_texture(textures.sky_sphere_cubemap);
      prepared.environment_systems.push_back({
        .system_type = static_cast<uint32_t>(
          data::pak::world::EnvironmentComponentType::kSkySphere),
        .record_bytes = PackRecordBytes(sphere),
      });
    }
    prepared.environment_references = std::move(references).Build();
    for (const auto& reference : source.references) {
      if (!ResolveAssetReference(context, reference.virtual_path,
            reference.type, reference.object_path)
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
  // The lambda is awaited in place and the session outlives it.
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
  auto placement
    = internal::PlaceAsset(Request(), data::kSceneDescriptorExtension);
  if (!placement.has_value()) {
    session.AddDiagnostic(std::move(placement.error()));
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene virtual path refused");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = SceneDescriptorExecutionContext {
    .session = session,
    .request = Request(),
    .mounts = {},
    .index_cache = {},
    .geometries = {},
  };
  context.mounts = internal::LoadMountedCookedSources(
    session, Request(), "scene.descriptor.");

  if (!CookedReader()
    || !co_await LoadSurfaceInventories(context, *source, *CookedReader())) {
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto textures = EnvironmentTextureIndices {};
  // The lambda is awaited in place and its references outlive each call.
  const auto resolve_texture
    = [&](const std::optional<std::string>& path, std::string object_path,
        const EnvironmentTextureUse use,
        data::TextureTarget& target) -> co::Co<bool> {
    if (!path.has_value()) {
      co_return true;
    }
    const auto resolved = co_await ResolveEnvironmentTexture(
      observer_ptr { &session }, observer_ptr { &Request() }, CookedReader(),
      context.mounts, *path, std::move(object_path), use);
    if (!resolved.has_value()) {
      co_return false;
    }
    target = *resolved;
    co_return true;
  };
  if (source->post_process.has_value()
    && !co_await resolve_texture(source->post_process->metering_mask,
      "environment.post_process_volume.auto_exposure_metering_mask",
      EnvironmentTextureUse::kMeteringMask, textures.metering_mask)) {
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (source->fog.has_value()
    && !co_await resolve_texture(source->fog->cubemap,
      "environment.fog.inscattering_color_cubemap_ref",
      EnvironmentTextureUse::kCubemap, textures.fog_cubemap)) {
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (source->sky_light.has_value()
    && !co_await resolve_texture(source->sky_light->cubemap,
      "environment.sky_light.cubemap_ref", EnvironmentTextureUse::kCubemap,
      textures.sky_light_cubemap)) {
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (source->sky_sphere.has_value()
    && !co_await resolve_texture(source->sky_sphere->cubemap,
      "environment.sky_sphere.cubemap_ref", EnvironmentTextureUse::kCubemap,
      textures.sky_sphere_cubemap)) {
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (!co_await ComposeInstances(context,
        data::AssetKey::FromVirtualPath(placement->virtual_path), *source,
        *CookedReader())) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Scene descriptor instances do not compose");
    co_return co_await FinalizeWithTelemetry(session);
  }
  auto prepared_opt
    = LinkSceneDescriptor(context, std::move(*source), textures);
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
  item.environment_references = std::move(prepared.environment_references);
  item.placement = std::move(*placement);

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
  auto& cooked = *result.cooked;
  session.AssetEmitter().Emit(cooked.scene_key, data::AssetType::kScene,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes,
    std::move(cooked.references));
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
