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
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::detail {

namespace {

  namespace lc = oxygen::content::lc;

  // Copying the sidecar strings and maps can only fail on allocation.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct ResolvedBufferSidecar final {
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    std::unordered_map<std::string, internal::BufferDescriptorView> views;
  };

  struct MountedInspection final {
    std::filesystem::path root;
    std::optional<lc::Inspection> inspection;
  };

  //! Borrowed for the duration of one job execution, which owns both.
  struct GeometryExecutionContext final {
    ImportSession& session; // NOLINT(*-avoid-const-or-ref-data-members)
    const ImportRequest& request; // NOLINT(*-avoid-const-or-ref-data-members)
    observer_ptr<IAsyncFileReader> reader;
    std::vector<MountedInspection> mounts;
    std::unordered_map<std::string, ResolvedBufferSidecar> buffer_cache;
    std::unordered_map<std::string, data::AssetKey> material_cache;
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

  auto LoadMountedInspections(GeometryExecutionContext& context) -> void
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
            ImportSeverity::kError, "geometry.descriptor.index_load_failed",
            "Failed loading cooked index: " + std::string(ex.what()),
            mount.root.string());
        }
      }

      context.mounts.push_back(std::move(mount));
    }
  }

  auto ValidateAndCopyName(ImportSession& session, const ImportRequest& request,
    const std::string_view source_name, const std::span<char> dest,
    std::string_view code, std::string_view message, std::string object_path)
    -> void
  {
    if (source_name.size() >= dest.size()) {
      AddDiagnostic(session, request, ImportSeverity::kWarning,
        std::string(code), std::string(message), std::move(object_path));
    }
    util::TruncateAndNullTerminate(dest, source_name);
  }

  template <typename T>
  auto WritePod(serio::AnyWriter& writer, const T& value) -> bool
  {
    return static_cast<bool>(
      writer.WriteBlob(std::as_bytes(std::span<const T, 1>(&value, 1))));
  }

  auto BuildAssetKey(const ImportRequest& request,
    const std::string_view virtual_path) -> data::AssetKey
  {
    static_cast<void>(request);
    return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
  }

  auto MakeDuration(const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  auto BuildViewMap(const std::span<const internal::BufferDescriptorView> views)
    -> std::unordered_map<std::string, internal::BufferDescriptorView>
  {
    auto map
      = std::unordered_map<std::string, internal::BufferDescriptorView> {};
    map.reserve(views.size());
    for (const auto& view : views) {
      map.insert_or_assign(view.name, view);
    }
    return map;
  }

  auto CacheLocalBufferResults(GeometryExecutionContext& context,
    const std::vector<BufferImportSubmitter::EmittedBuffer>& emitted_buffers)
    -> void
  {
    for (const auto& emitted : emitted_buffers) {
      context.buffer_cache.insert_or_assign(emitted.source_id,
        ResolvedBufferSidecar {
          .resource_index = emitted.resource_index,
          .descriptor = emitted.descriptor,
          .views = BuildViewMap(emitted.views),
        });
    }
  }

  // The caller awaits each resolution inline while it owns `context`.
  // NOLINTNEXTLINE(*-avoid-reference-coroutine-parameters)
  auto ResolveBufferSidecarByVirtualPath(GeometryExecutionContext& context,
    std::string_view virtual_path, std::string object_path)
    -> co::Co<std::optional<ResolvedBufferSidecar>>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.virtual_path_invalid",
        "Buffer reference virtual_path must be canonical",
        std::move(object_path));
      co_return std::nullopt;
    }

    if (const auto it = context.buffer_cache.find(std::string(virtual_path));
      it != context.buffer_cache.end()) {
      co_return it->second;
    }

    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          context.request, virtual_path, relpath)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.virtual_path_unmounted",
        "Buffer reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      co_return std::nullopt;
    }

    if (context.reader == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.reader_unavailable",
        "Async file reader is not available", std::move(object_path));
      co_return std::nullopt;
    }

    auto matches = std::vector<observer_ptr<const MountedInspection>> {};
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      auto ec = std::error_code {};
      if (!std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }
      matches.emplace_back(&*it);
    }

    if (matches.size() > 1U) {
      auto message = std::string {
        "Buffer descriptor virtual_path resolved to multiple mounted "
        "sidecars; provide a single canonical source: "
      } + std::string(virtual_path);
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.sidecar_ambiguous", std::move(message), object_path);
      co_return std::nullopt;
    }

    if (!matches.empty()) {
      const auto& matched = *matches.front();
      if (base::PathIdentityKey(std::filesystem::weakly_canonical(matched.root))
        != base::PathIdentityKey(
          std::filesystem::weakly_canonical(context.session.CookedRoot()))) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.buffer.foreign_root",
          "Raw geometry buffers must belong to the geometry's cooked root. "
          "Reference the library geometry asset, or import the buffer source "
          "into this root.",
          std::move(object_path));
        co_return std::nullopt;
      }
      const auto descriptor_path
        = matched.root / std::filesystem::path(relpath);
      const auto read_start = std::chrono::steady_clock::now();
      const auto read_result
        = co_await context.reader->ReadFile(descriptor_path);
      context.session.AddLoadDuration(
        MakeDuration(read_start, std::chrono::steady_clock::now()));
      if (!read_result.has_value()) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.buffer.sidecar_read_failed",
          "Failed reading buffer descriptor: " + read_result.error().ToString(),
          object_path);
        co_return std::nullopt;
      }

      auto parsed = internal::ParsedBufferDescriptorSidecar {};
      auto parse_error = std::string {};
      if (!internal::ParseBufferDescriptorSidecar(
            read_result.value(), parsed, parse_error)) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.buffer.sidecar_invalid", parse_error, object_path);
        co_return std::nullopt;
      }

      auto resolved = ResolvedBufferSidecar {
        .resource_index = parsed.resource_index,
        .descriptor = parsed.descriptor,
        .views = BuildViewMap(parsed.views),
      };
      context.buffer_cache.insert_or_assign(
        std::string(virtual_path), resolved);
      co_return resolved;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      "geometry.buffer.sidecar_missing",
      "Buffer descriptor virtual_path was not found: "
        + std::string(virtual_path),
      std::move(object_path));
    co_return std::nullopt;
  }

  auto ResolveMaterialKeyByVirtualPath(GeometryExecutionContext& context,
    std::string_view virtual_path, std::string object_path)
    -> std::optional<data::AssetKey>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.material.virtual_path_invalid",
        "Material reference virtual_path must be canonical",
        std::move(object_path));
      return std::nullopt;
    }

    if (const auto it = context.material_cache.find(std::string(virtual_path));
      it != context.material_cache.end()) {
      return it->second;
    }

    auto relpath = std::string {};
    const auto is_mounted = internal::TryVirtualPathToRelPath(
      context.request, virtual_path, relpath);
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (it->inspection.has_value()) {
        for (const auto& asset : it->inspection->Assets()) {
          if (asset.virtual_path != virtual_path) {
            continue;
          }

          const auto type = static_cast<data::AssetType>(asset.asset_type);
          if (type != data::AssetType::kMaterial) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError, "geometry.material.type_mismatch",
              "Virtual path does not reference a material descriptor",
              object_path);
            return std::nullopt;
          }

          context.material_cache.insert_or_assign(
            std::string(virtual_path), asset.key);
          return asset.key;
        }
      }
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (!is_mounted || !std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }

      const auto key = oxygen::data::AssetKey::FromVirtualPath(virtual_path);
      context.material_cache.insert_or_assign(std::string(virtual_path), key);
      return key;
    }

    if (!is_mounted) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.material.virtual_path_unmounted",
        "Material reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      "geometry.material.missing",
      "Material descriptor virtual_path was not found: "
        + std::string(virtual_path),
      std::move(object_path));
    return std::nullopt;
  }

  auto ResolveAssetKeyByVirtualPath(GeometryExecutionContext& context,
    std::string_view virtual_path, std::string object_path)
    -> std::optional<data::AssetKey>
  {
    if (!internal::IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.asset.virtual_path_invalid",
        "Asset reference virtual_path must be canonical",
        std::move(object_path));
      return std::nullopt;
    }

    auto relpath = std::string {};
    const auto is_mounted = internal::TryVirtualPathToRelPath(
      context.request, virtual_path, relpath);
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (it->inspection.has_value()) {
        for (const auto& asset : it->inspection->Assets()) {
          if (asset.virtual_path == virtual_path) {
            return asset.key;
          }
        }
      }
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (!is_mounted || !std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }

      return oxygen::data::AssetKey::FromVirtualPath(virtual_path);
    }

    if (!is_mounted) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.asset.virtual_path_unmounted",
        "Asset reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      "geometry.asset.missing",
      "Asset descriptor virtual_path was not found: "
        + std::string(virtual_path),
      std::move(object_path));
    return std::nullopt;
  }

  auto CheckedU32(const uint64_t value, ImportSession& session,
    const ImportRequest& request, std::string_view code,
    std::string_view message, std::string object_path)
    -> std::optional<uint32_t>
  {
    if (value > std::numeric_limits<uint32_t>::max()) {
      AddDiagnostic(session, request, ImportSeverity::kError, std::string(code),
        std::string(message), std::move(object_path));
      return std::nullopt;
    }
    return static_cast<uint32_t>(value);
  }

  auto FindBufferView(const ResolvedBufferSidecar& sidecar,
    std::string_view view_name) -> const internal::BufferDescriptorView*
  {
    const auto it = sidecar.views.find(std::string(view_name));
    if (it == sidecar.views.end()) {
      return nullptr;
    }
    return &it->second;
  }

  auto ResolveMeshViewPair(GeometryExecutionContext& context,
    const ResolvedBufferSidecar& vb_sidecar,
    const ResolvedBufferSidecar& ib_sidecar, std::string_view view_ref,
    const std::string& object_path,
    data::pak::geometry::MeshViewDesc& out_view_desc) -> bool
  {
    const auto* const vb_view = FindBufferView(vb_sidecar, view_ref);
    if (vb_view == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.view_missing",
        "Vertex buffer view_ref was not found: " + std::string(view_ref),
        object_path + ".view_ref");
      return false;
    }

    const auto* const ib_view = FindBufferView(ib_sidecar, view_ref);
    if (ib_view == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.buffer.view_missing",
        "Index buffer view_ref was not found: " + std::string(view_ref),
        object_path + ".view_ref");
      return false;
    }

    const auto first_index = CheckedU32(ib_view->element_offset,
      context.session, context.request, "geometry.buffer.view_overflow",
      "Mesh view first_index exceeds supported range",
      object_path + ".view_ref");
    const auto index_count = CheckedU32(ib_view->element_count, context.session,
      context.request, "geometry.buffer.view_overflow",
      "Mesh view index_count exceeds supported range",
      object_path + ".view_ref");
    const auto first_vertex = CheckedU32(vb_view->element_offset,
      context.session, context.request, "geometry.buffer.view_overflow",
      "Mesh view first_vertex exceeds supported range",
      object_path + ".view_ref");
    const auto vertex_count = CheckedU32(vb_view->element_count,
      context.session, context.request, "geometry.buffer.view_overflow",
      "Mesh view vertex_count exceeds supported range",
      object_path + ".view_ref");

    if (!first_index.has_value() || !index_count.has_value()
      || !first_vertex.has_value() || !vertex_count.has_value()) {
      return false;
    }

    out_view_desc.first_index = *first_index;
    out_view_desc.index_count = *index_count;
    out_view_desc.first_vertex = *first_vertex;
    out_view_desc.vertex_count = *vertex_count;
    return true;
  }

  auto EnsureBufferUsageFlags(GeometryExecutionContext& context,
    const ResolvedBufferSidecar& sidecar,
    const data::BufferResource::UsageFlags required_flag,
    std::string_view object_path, std::string_view logical_name) -> bool
  {
    const auto usage_flags = static_cast<data::BufferResource::UsageFlags>(
      sidecar.descriptor.usage_flags);
    if ((usage_flags & required_flag) == required_flag) {
      return true;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      "geometry.buffer.usage_mismatch",
      std::string(logical_name) + " does not advertise required usage flag",
      std::string(object_path));
    return false;
  }

  struct LinkedGeometryDescriptor final {
    std::string geometry_name;
    std::vector<std::byte> descriptor_bytes;
    std::vector<MeshBufferBindings> lod_bindings;
  };

  // The caller awaits the link inline while it owns `context` and `source`.
  // NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
  auto LinkGeometryDescriptor(
    GeometryExecutionContext& context, const internal::GeometrySource& source)
    -> co::Co<std::optional<LinkedGeometryDescriptor>>
  // NOLINTEND(*-avoid-reference-coroutine-parameters)
  {
    auto prepared = LinkedGeometryDescriptor {};
    prepared.geometry_name = source.name;
    const auto& root_bounds = source.bounds;
    const auto lod_count = source.lods.size();

    auto output_stream = serio::MemoryStream {};
    auto writer = serio::Writer(output_stream);
    const auto pack = writer.ScopedAlignment(1);

    auto asset_desc = data::pak::geometry::GeometryAssetDesc {};
    asset_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    asset_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    asset_desc.header.variant_flags = 0;
    asset_desc.lod_count = static_cast<uint32_t>(lod_count);
    std::ranges::copy(root_bounds.min, std::begin(asset_desc.bounding_box_min));
    std::ranges::copy(root_bounds.max, std::begin(asset_desc.bounding_box_max));
    ValidateAndCopyName(context.session, context.request,
      prepared.geometry_name, std::span(asset_desc.header.name),
      "geometry.descriptor.name_truncated",
      "Geometry name truncated to fit descriptor limit", "name");

    if (!WritePod(writer, asset_desc)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.descriptor.serialize_failed",
        "Failed writing geometry descriptor header");
      co_return std::nullopt;
    }

    prepared.lod_bindings.reserve(lod_count);

    for (size_t lod_i = 0; lod_i < lod_count; ++lod_i) {
      const auto& lod = source.lods.at(lod_i);
      const auto lod_path = "lods[" + std::to_string(lod_i) + "]";
      const auto& lod_bounds = lod.bounds;
      auto mesh_desc = data::pak::geometry::MeshDesc {};
      ValidateAndCopyName(context.session, context.request, lod.name,
        std::span(mesh_desc.name), "geometry.descriptor.lod_name_truncated",
        "LOD name truncated to fit descriptor limit", lod_path + ".name");

      auto mesh_bindings = MeshBufferBindings {};
      std::span<const std::byte> procedural_blob;
      std::optional<ResolvedBufferSidecar> resolved_vb {};
      std::optional<ResolvedBufferSidecar> resolved_ib {};
      uint32_t procedural_vertex_count = 0;
      uint32_t procedural_index_count = 0;

      const auto* skinned
        = std::get_if<internal::GeometrySource::Skinned>(&lod.mesh);
      const auto* buffers = skinned != nullptr
        ? &skinned->buffers
        : std::get_if<internal::GeometrySource::Buffers>(&lod.mesh);
      if (buffers != nullptr) {
        const auto& vb_ref = buffers->vertex;
        const auto& ib_ref = buffers->index;

        auto vb_sidecar_opt = co_await ResolveBufferSidecarByVirtualPath(
          context, vb_ref, lod_path + ".buffers.vb_ref");
        auto ib_sidecar_opt = co_await ResolveBufferSidecarByVirtualPath(
          context, ib_ref, lod_path + ".buffers.ib_ref");
        if (!vb_sidecar_opt.has_value() || !ib_sidecar_opt.has_value()) {
          co_return std::nullopt;
        }

        if (!EnsureBufferUsageFlags(context, *vb_sidecar_opt,
              data::BufferResource::UsageFlags::kVertexBuffer,
              lod_path + ".buffers.vb_ref", "Vertex buffer")
          || !EnsureBufferUsageFlags(context, *ib_sidecar_opt,
            data::BufferResource::UsageFlags::kIndexBuffer,
            lod_path + ".buffers.ib_ref", "Index buffer")) {
          co_return std::nullopt;
        }

        resolved_vb = *vb_sidecar_opt;
        resolved_ib = *ib_sidecar_opt;
        mesh_bindings.vertex_buffer = vb_sidecar_opt->resource_index;
        mesh_bindings.index_buffer = ib_sidecar_opt->resource_index;

        if (skinned != nullptr) {
          mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kSkinned);

          auto joint_index_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skinned->joint_index, lod_path + ".skinning.joint_index_ref");
          auto joint_weight_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skinned->joint_weight, lod_path + ".skinning.joint_weight_ref");
          auto inverse_bind_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skinned->inverse_bind, lod_path + ".skinning.inverse_bind_ref");
          auto joint_remap_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skinned->joint_remap, lod_path + ".skinning.joint_remap_ref");
          if (!joint_index_sidecar.has_value()
            || !joint_weight_sidecar.has_value()
            || !inverse_bind_sidecar.has_value()
            || !joint_remap_sidecar.has_value()) {
            co_return std::nullopt;
          }

          mesh_bindings.joint_index_buffer
            = joint_index_sidecar->resource_index;
          mesh_bindings.joint_weight_buffer
            = joint_weight_sidecar->resource_index;
          mesh_bindings.inverse_bind_buffer
            = inverse_bind_sidecar->resource_index;
          mesh_bindings.joint_remap_buffer
            = joint_remap_sidecar->resource_index;

          mesh_desc.info.skinned.vertex_buffer = data::kNoResourceReference;
          mesh_desc.info.skinned.index_buffer = data::kNoResourceReference;
          mesh_desc.info.skinned.joint_index_buffer
            = data::kNoResourceReference;
          mesh_desc.info.skinned.joint_weight_buffer
            = data::kNoResourceReference;
          mesh_desc.info.skinned.inverse_bind_buffer
            = data::kNoResourceReference;
          mesh_desc.info.skinned.joint_remap_buffer
            = data::kNoResourceReference;
          mesh_desc.info.skinned.joint_count = skinned->joint_count;
          mesh_desc.info.skinned.influences_per_vertex
            = skinned->influences_per_vertex;
          mesh_desc.info.skinned.flags = skinned->flags;
          if (skinned->skeleton.has_value()) {
            const auto& skeleton_ref = *skinned->skeleton;
            const auto skeleton_key = ResolveAssetKeyByVirtualPath(
              context, skeleton_ref, lod_path + ".skinning.skeleton_ref");
            if (!skeleton_key.has_value()) {
              AddDiagnostic(context.session, context.request,
                ImportSeverity::kError, "geometry.skinning.skeleton_missing",
                "skeleton_ref could not be resolved",
                lod_path + ".skinning.skeleton_ref");
              co_return std::nullopt;
            }
            mesh_desc.info.skinned.skeleton_asset_key = *skeleton_key;
          }
          std::ranges::copy(lod_bounds.min,
            std::begin(mesh_desc.info.skinned.bounding_box_min));
          std::ranges::copy(lod_bounds.max,
            std::begin(mesh_desc.info.skinned.bounding_box_max));
        } else {
          mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kStandard);
          mesh_desc.info.standard.vertex_buffer = data::kNoResourceReference;
          mesh_desc.info.standard.index_buffer = data::kNoResourceReference;
          std::ranges::copy(lod_bounds.min,
            std::begin(mesh_desc.info.standard.bounding_box_min));
          std::ranges::copy(lod_bounds.max,
            std::begin(mesh_desc.info.standard.bounding_box_max));
        }
      } else {
        mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kProcedural);
        const auto& procedural
          = std::get<internal::GeometrySource::Procedural>(lod.mesh);
        const auto& procedural_name = procedural.name;
        procedural_blob = procedural.parameters;
        ValidateAndCopyName(context.session, context.request, procedural_name,
          std::span(mesh_desc.name), "geometry.procedural.name_truncated",
          "Procedural mesh name truncated to fit descriptor limit",
          lod_path + ".procedural.mesh_name");

        mesh_desc.info.procedural.params_size
          = static_cast<uint32_t>(procedural_blob.size());

        const auto generated = data::GenerateMeshBuffers(
          procedural_name, std::span<const std::byte>(procedural_blob));
        if (!generated.has_value()) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "geometry.procedural.generation_failed",
            "Procedural mesh generator rejected descriptor parameters",
            lod_path + ".procedural");
          co_return std::nullopt;
        }
        const auto vtx_count
          = CheckedU32(generated->first.size(), context.session,
            context.request, "geometry.procedural.mesh_too_large",
            "Generated procedural vertex count exceeds supported range",
            lod_path + ".procedural");
        const auto idx_count
          = CheckedU32(generated->second.size(), context.session,
            context.request, "geometry.procedural.mesh_too_large",
            "Generated procedural index count exceeds supported range",
            lod_path + ".procedural");
        if (!vtx_count.has_value() || !idx_count.has_value()) {
          co_return std::nullopt;
        }
        procedural_vertex_count = *vtx_count;
        procedural_index_count = *idx_count;
      }

      const auto& submeshes = lod.submeshes;
      mesh_desc.submesh_count = static_cast<uint32_t>(submeshes.size());
      uint64_t total_mesh_views = 0;
      for (const auto& submesh : submeshes) {
        total_mesh_views += submesh.views.size();
      }
      const auto mesh_view_count = CheckedU32(total_mesh_views, context.session,
        context.request, "geometry.descriptor.view_count_overflow",
        "Mesh view count exceeds supported range", lod_path + ".submeshes");
      if (!mesh_view_count.has_value()) {
        co_return std::nullopt;
      }
      mesh_desc.mesh_view_count = *mesh_view_count;

      if (!WritePod(writer, mesh_desc)) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.descriptor.serialize_failed",
          "Failed writing mesh descriptor", lod_path);
        co_return std::nullopt;
      }

      if (mesh_desc.IsProcedural() && (!procedural_blob.empty())) {
        if (!writer.WriteBlob(std::as_bytes(std::span<const std::byte>(
              procedural_blob.data(), procedural_blob.size())))) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "geometry.descriptor.serialize_failed",
            "Failed writing procedural parameter blob",
            lod_path + ".procedural.params");
          co_return std::nullopt;
        }
      }

      for (size_t submesh_i = 0; submesh_i < submeshes.size(); ++submesh_i) {
        const auto& submesh = submeshes.at(submesh_i);
        const auto submesh_path
          = lod_path + ".submeshes[" + std::to_string(submesh_i) + "]";
        auto submesh_desc = data::pak::geometry::SubMeshDesc {};
        submesh_desc.slot_id = submesh.slot_id;
        const auto& submesh_name = submesh.name;
        ValidateAndCopyName(context.session, context.request, submesh_name,
          std::span(submesh_desc.name),
          "geometry.descriptor.submesh_name_truncated",
          "Submesh name truncated to fit descriptor limit",
          submesh_path + ".name");

        const auto& material_ref = submesh.material;
        const auto material_key = ResolveMaterialKeyByVirtualPath(
          context, material_ref, submesh_path + ".material_ref");
        if (!material_key.has_value()) {
          co_return std::nullopt;
        }
        submesh_desc.material_asset_key = *material_key;
        submesh_desc.mesh_view_count
          = static_cast<uint32_t>(submesh.views.size());

        const auto& submesh_bounds = submesh.bounds;

        std::ranges::copy(
          submesh_bounds.min, std::begin(submesh_desc.bounding_box_min));
        std::ranges::copy(
          submesh_bounds.max, std::begin(submesh_desc.bounding_box_max));

        if (!WritePod(writer, submesh_desc)) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "geometry.descriptor.serialize_failed",
            "Failed writing submesh descriptor", submesh_path);
          co_return std::nullopt;
        }

        for (size_t view_i = 0; view_i < submesh.views.size(); ++view_i) {
          const auto& view = submesh.views.at(view_i);
          const auto view_path
            = submesh_path + ".views[" + std::to_string(view_i) + "]";
          auto view_desc = data::pak::geometry::MeshViewDesc {};

          if (mesh_desc.IsProcedural()) {
            view_desc.first_index = 0;
            view_desc.index_count = procedural_index_count;
            view_desc.first_vertex = 0;
            view_desc.vertex_count = procedural_vertex_count;
          } else {
            if (!resolved_vb.has_value() || !resolved_ib.has_value()) {
              co_return std::nullopt;
            }

            if (!ResolveMeshViewPair(context, *resolved_vb, *resolved_ib,
                  view.view_ref, view_path, view_desc)) {
              co_return std::nullopt;
            }
          }
          std::ranges::copy(
            view.bounds.min, std::begin(view_desc.bounding_box_min));
          std::ranges::copy(
            view.bounds.max, std::begin(view_desc.bounding_box_max));

          if (!WritePod(writer, view_desc)) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError, "geometry.descriptor.serialize_failed",
              "Failed writing mesh view descriptor", view_path);
            co_return std::nullopt;
          }
        }
      }

      prepared.lod_bindings.push_back(mesh_bindings);
    }

    const auto bytes = output_stream.Data();
    prepared.descriptor_bytes.assign(bytes.begin(), bytes.end());
    co_return prepared;
  }

} // namespace

auto GeometryDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting geometry descriptor job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
  // Awaited inline below, while the job owns `session` and this closure.
  // NOLINTBEGIN(*-avoid-reference-coroutine-parameters,*-avoid-capturing-lambda-coroutines)
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    // NOLINTEND(*-avoid-reference-coroutine-parameters,*-avoid-capturing-lambda-coroutines)
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

  const auto& descriptor = Request().geometry_descriptor;
  if (!descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "geometry.descriptor.request_invalid",
      "GeometryDescriptorImportJob requires request geometry_descriptor "
      "payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid geometry descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto source = internal::GeometrySource::FromDescriptor(
    descriptor->normalized_descriptor_json, Request().source_path, diagnostics);
  AddDiagnostics(session, std::move(diagnostics));
  if (!source.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor preparation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = GeometryExecutionContext {
    .session = session,
    .request = Request(),
    .reader = CookedReader(),
    .mounts = {},
    .buffer_cache = {},
    .material_cache = {},
  };
  LoadMountedInspections(context);

  if (!source->buffers.empty()) {
    auto& pipeline = CreatePipeline<BufferPipeline>(*ThreadPool(),
      BufferPipeline::Config {
        .queue_capacity = Concurrency().buffer.queue_capacity,
        .worker_count = Concurrency().buffer.workers,
        .with_content_hashing = EffectiveContentHashingEnabled(
          Request().options.with_content_hashing),
      });

    auto submitter
      = BufferImportSubmitter(session, Request(), FileReader(), StopToken());
    const auto submission
      = co_await submitter.SubmitBuffers(source->buffers, pipeline);
    pipeline.Close();

    if (submission.submitted_count == 0U) {
      if (!session.HasErrors()) {
        AddDiagnostic(session, Request(), ImportSeverity::kError,
          "geometry.buffer.no_submissions",
          "No buffer work items were submitted");
      }
      ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
        "Geometry descriptor buffer preparation failed");
      co_return co_await FinalizeWithTelemetry(session);
    }

    const auto emitted
      = co_await submitter.CollectAndEmit(pipeline, submission);
    CacheLocalBufferResults(context, emitted);
  }

  const auto prepared_opt = co_await LinkGeometryDescriptor(context, *source);
  if (!prepared_opt.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto& prepared = *prepared_opt;
  auto finalizer = GeometryPipeline(*ThreadPool(),
    GeometryPipeline::Config {
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  auto finalize_diagnostics = std::vector<ImportDiagnostic> {};
  auto finalized = co_await finalizer.FinalizeDescriptor(prepared.lod_bindings,
    prepared.descriptor_bytes,
    std::span<const GeometryPipeline::MaterialKeyPatch> {},
    finalize_diagnostics);
  AddDiagnostics(session, std::move(finalize_diagnostics));

  if (!finalized.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor finalization failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  if (session.HasErrors()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto virtual_path
    = Request().loose_cooked_layout.GeometryVirtualPath(prepared.geometry_name);
  const auto descriptor_relpath
    = Request().loose_cooked_layout.GeometryDescriptorRelPath(
      prepared.geometry_name);
  const auto geometry_key = BuildAssetKey(Request(), virtual_path);

  const auto emit_start = std::chrono::steady_clock::now();
  session.AssetEmitter().Emit(geometry_key, data::AssetType::kGeometry,
    virtual_path, descriptor_relpath, finalized->bytes,
    std::move(finalized->references));
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

// The job awaits finalization inline while it owns `session`.
// NOLINTNEXTLINE(*-avoid-reference-coroutine-parameters)
auto GeometryDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
