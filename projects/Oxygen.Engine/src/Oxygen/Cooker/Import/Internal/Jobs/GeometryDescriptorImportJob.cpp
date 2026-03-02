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
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;
  namespace lc = oxygen::content::lc;

  struct Bounds3 final {
    std::array<float, 3> min {};
    std::array<float, 3> max {};
  };

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

  struct GeometryExecutionContext final {
    ImportSession& session;
    const ImportRequest& request;
    observer_ptr<IAsyncFileReader> reader {};
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

  auto GetGeometryDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(json::parse(kGeometryDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "geometry.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Geometry descriptor validation failed: ",
      .validation_overflow_prefix = "Geometry descriptor validation emitted ",
      .validator_failure_code = "geometry.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Geometry descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetGeometryDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, object_path);
      });
  }

  auto ValidateNoDotSegments(const std::string_view path) -> bool
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == "." || segment == "..") {
        return false;
      }
      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
    return true;
  }

  auto IsCanonicalVirtualPath(const std::string_view virtual_path) -> bool
  {
    if (virtual_path.empty()) {
      return false;
    }
    if (virtual_path.front() != '/') {
      return false;
    }
    if (virtual_path.find('\\') != std::string_view::npos) {
      return false;
    }
    if (virtual_path.find("//") != std::string_view::npos) {
      return false;
    }
    if (virtual_path.size() > 1 && virtual_path.back() == '/') {
      return false;
    }
    return ValidateNoDotSegments(virtual_path);
  }

  auto NormalizeMountRoot(std::string mount_root) -> std::string
  {
    if (mount_root.empty()) {
      return "/";
    }
    if (mount_root.front() != '/') {
      mount_root.insert(mount_root.begin(), '/');
    }
    while (mount_root.size() > 1 && mount_root.back() == '/') {
      mount_root.pop_back();
    }
    return mount_root;
  }

  auto TryVirtualPathToRelPath(const ImportRequest& request,
    const std::string_view virtual_path, std::string& relpath) -> bool
  {
    const auto mount_root
      = NormalizeMountRoot(request.loose_cooked_layout.virtual_mount_root);
    if (!virtual_path.starts_with(mount_root)) {
      return false;
    }
    if (virtual_path.size() == mount_root.size()) {
      relpath.clear();
      return false;
    }
    const auto slash_pos = mount_root.size();
    if (virtual_path[slash_pos] != '/') {
      return false;
    }
    relpath = std::string(virtual_path.substr(slash_pos + 1));
    return !relpath.empty();
  }

  auto LoadMountedInspections(GeometryExecutionContext& context) -> void
  {
    auto seen = std::unordered_set<std::string> {};

    const auto append_root = [&](const std::filesystem::path& root) {
      auto normalized = root.lexically_normal();
      const auto key = normalized.generic_string();
      if (!seen.insert(key).second) {
        return;
      }

      auto mount = MountedInspection {
        .root = std::move(normalized),
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
    };

    if (context.request.cooked_root.has_value()) {
      append_root(*context.request.cooked_root);
    } else {
      append_root(context.request.source_path.parent_path());
    }
    for (const auto& root : context.request.cooked_context_roots) {
      append_root(root);
    }
  }

  auto ParseBounds(const json& bounds_doc, Bounds3& out_bounds) -> bool
  {
    if (!bounds_doc.is_object()) {
      return false;
    }
    if (!bounds_doc.contains("min") || !bounds_doc.contains("max")) {
      return false;
    }

    const auto parse_vec3 = [](const json& value, std::array<float, 3>& out) {
      if (!value.is_array() || value.size() != 3U) {
        return false;
      }
      for (size_t i = 0; i < 3U; ++i) {
        if (!value[i].is_number()) {
          return false;
        }
        out[i] = value[i].get<float>();
      }
      return true;
    };

    return parse_vec3(bounds_doc.at("min"), out_bounds.min)
      && parse_vec3(bounds_doc.at("max"), out_bounds.max);
  }

  auto ValidateAndCopyName(ImportSession& session, const ImportRequest& request,
    const std::string_view source_name, char* dest, const size_t dest_size,
    std::string_view code, std::string_view message, std::string object_path)
    -> void
  {
    if (source_name.size() >= dest_size) {
      AddDiagnostic(session, request, ImportSeverity::kWarning,
        std::string(code), std::string(message), std::move(object_path));
    }
    util::TruncateAndNullTerminate(dest, dest_size, source_name);
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
    switch (request.options.asset_key_policy) {
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      break;
    }
    return util::MakeDeterministicAssetKey(virtual_path);
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

  auto ResolveBufferSidecarByVirtualPath(GeometryExecutionContext& context,
    std::string_view virtual_path, std::string object_path)
    -> co::Co<std::optional<ResolvedBufferSidecar>>
  {
    if (!IsCanonicalVirtualPath(virtual_path)) {
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
    if (!TryVirtualPathToRelPath(context.request, virtual_path, relpath)) {
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

    auto matches = std::vector<std::filesystem::path> {};
    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      auto ec = std::error_code {};
      if (!std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }
      matches.push_back(descriptor_path);
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
      const auto& descriptor_path = matches.front();
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
    if (!IsCanonicalVirtualPath(virtual_path)) {
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

    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (!it->inspection.has_value()) {
        continue;
      }
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

    auto relpath = std::string {};
    if (!TryVirtualPathToRelPath(context.request, virtual_path, relpath)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.material.virtual_path_unmounted",
        "Material reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (!std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }

      if (context.request.options.asset_key_policy == AssetKeyPolicy::kRandom) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.material.index_resolution_required",
          "Material reference requires index resolution when asset_key_policy "
          "is random",
          std::move(object_path));
        return std::nullopt;
      }

      const auto key = util::MakeDeterministicAssetKey(virtual_path);
      context.material_cache.insert_or_assign(std::string(virtual_path), key);
      return key;
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
    if (!IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.asset.virtual_path_invalid",
        "Asset reference virtual_path must be canonical",
        std::move(object_path));
      return std::nullopt;
    }

    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      if (!it->inspection.has_value()) {
        continue;
      }
      for (const auto& asset : it->inspection->Assets()) {
        if (asset.virtual_path == virtual_path) {
          return asset.key;
        }
      }
    }

    auto relpath = std::string {};
    if (!TryVirtualPathToRelPath(context.request, virtual_path, relpath)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.asset.virtual_path_unmounted",
        "Asset reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      return std::nullopt;
    }

    for (auto it = context.mounts.rbegin(); it != context.mounts.rend(); ++it) {
      const auto descriptor_path = it->root / std::filesystem::path(relpath);
      std::error_code ec;
      if (!std::filesystem::exists(descriptor_path, ec)) {
        continue;
      }

      if (context.request.options.asset_key_policy == AssetKeyPolicy::kRandom) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.asset.index_resolution_required",
          "Asset reference requires index resolution when asset_key_policy is "
          "random",
          std::move(object_path));
        return std::nullopt;
      }
      return util::MakeDeterministicAssetKey(virtual_path);
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
    if (value > (std::numeric_limits<uint32_t>::max)()) {
      AddDiagnostic(session, request, ImportSeverity::kError, std::string(code),
        std::string(message), std::move(object_path));
      return std::nullopt;
    }
    return static_cast<uint32_t>(value);
  }

  auto BuildProceduralParamBlob(const json& lod_doc,
    const std::string_view object_path, std::string& full_mesh_name,
    std::vector<std::byte>& out_blob) -> bool
  {
    static_cast<void>(object_path);
    if (!lod_doc.contains("procedural")
      || !lod_doc.at("procedural").is_object()) {
      return false;
    }
    const auto& proc = lod_doc.at("procedural");
    const auto generator = proc.at("generator").get<std::string>();
    const auto mesh_name = proc.at("mesh_name").get<std::string>();
    full_mesh_name = generator + "/" + mesh_name;

    out_blob.clear();
    auto stream = serio::MemoryStream {};
    auto writer = serio::Writer(stream);
    const auto pack = writer.ScopedAlignment(1);

    const auto has_params
      = proc.contains("params") && proc.at("params").is_object();
    const auto& params = has_params ? proc.at("params") : json::object();

    const auto write_u32 = [&writer](const std::string& key, const json& obj,
                             const uint32_t default_value) {
      const auto value
        = obj.contains(key) ? obj.at(key).get<uint32_t>() : default_value;
      return WritePod(writer, value);
    };
    const auto write_f32 = [&writer](const std::string& key, const json& obj,
                             const float default_value) {
      const auto value
        = obj.contains(key) ? obj.at(key).get<float>() : default_value;
      return WritePod(writer, value);
    };

    auto ok = true;
    if (generator == "Sphere") {
      ok = write_u32("latitude_segments", params, 16U)
        && write_u32("longitude_segments", params, 32U);
    } else if (generator == "Plane") {
      ok = write_u32("x_segments", params, 1U)
        && write_u32("z_segments", params, 1U)
        && write_f32("size", params, 1.0F);
    } else if (generator == "Cylinder") {
      ok = write_u32("segments", params, 16U)
        && write_f32("height", params, 1.0F)
        && write_f32("radius", params, 0.5F);
    } else if (generator == "Cone") {
      ok = write_u32("segments", params, 16U)
        && write_f32("height", params, 1.0F)
        && write_f32("radius", params, 0.5F);
    } else if (generator == "Torus") {
      ok = write_u32("major_segments", params, 32U)
        && write_u32("minor_segments", params, 16U)
        && write_f32("major_radius", params, 1.0F)
        && write_f32("minor_radius", params, 0.25F);
    } else if (generator == "Quad") {
      ok
        = write_f32("width", params, 1.0F) && write_f32("height", params, 1.0F);
    } else {
      // Cube and ArrowGizmo do not consume params.
      ok = true;
    }

    if (!ok) {
      static_cast<void>(object_path);
      return false;
    }

    const auto bytes = stream.Data();
    out_blob.assign(bytes.begin(), bytes.end());
    return true;
  }

  auto BuildSubmitterBufferChunks(const json& descriptor_doc) -> json
  {
    if (!descriptor_doc.contains("buffers")) {
      return json {};
    }

    auto chunks = json::array();
    for (const auto& authored : descriptor_doc.at("buffers")) {
      auto chunk = authored;
      chunk["source"] = authored.at("uri");
      chunk.erase("uri");
      chunks.push_back(std::move(chunk));
    }
    return chunks;
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

  struct PreparedGeometryDescriptor final {
    std::string geometry_name;
    std::vector<std::byte> descriptor_bytes;
    std::vector<MeshBufferBindings> lod_bindings;
  };

  auto PrepareGeometryDescriptor(
    GeometryExecutionContext& context, const nlohmann::json& descriptor_doc)
    -> co::Co<std::optional<PreparedGeometryDescriptor>>
  {
    auto prepared = PreparedGeometryDescriptor {};
    prepared.geometry_name = descriptor_doc.at("name").get<std::string>();
    const auto root_bounds_doc = descriptor_doc.at("bounds");
    auto root_bounds = Bounds3 {};
    if (!ParseBounds(root_bounds_doc, root_bounds)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.descriptor.bounds_invalid",
        "Descriptor bounds must be an object with numeric min/max vec3",
        "bounds");
      co_return std::nullopt;
    }

    const auto& lods_doc = descriptor_doc.at("lods");
    const auto lod_count = lods_doc.size();
    if (lod_count == 0U || lod_count > 8U) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.descriptor.lod_count_invalid",
        "Descriptor must define between 1 and 8 LODs", "lods");
      co_return std::nullopt;
    }

    auto output_stream = serio::MemoryStream {};
    auto writer = serio::Writer(output_stream);
    const auto pack = writer.ScopedAlignment(1);

    auto asset_desc = data::pak::geometry::GeometryAssetDesc {};
    asset_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    asset_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    asset_desc.header.variant_flags = 0;
    asset_desc.lod_count = static_cast<uint32_t>(lod_count);
    std::copy_n(root_bounds.min.data(), 3, asset_desc.bounding_box_min);
    std::copy_n(root_bounds.max.data(), 3, asset_desc.bounding_box_max);
    ValidateAndCopyName(context.session, context.request,
      prepared.geometry_name, asset_desc.header.name,
      std::size(asset_desc.header.name), "geometry.descriptor.name_truncated",
      "Geometry name truncated to fit descriptor limit", "name");

    if (!WritePod(writer, asset_desc)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        "geometry.descriptor.serialize_failed",
        "Failed writing geometry descriptor header");
      co_return std::nullopt;
    }

    prepared.lod_bindings.reserve(lod_count);

    for (size_t lod_i = 0; lod_i < lod_count; ++lod_i) {
      const auto& lod_doc = lods_doc.at(lod_i);
      const auto lod_path = "lods[" + std::to_string(lod_i) + "]";

      auto lod_bounds = root_bounds;
      if (!ParseBounds(lod_doc.at("bounds"), lod_bounds)) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          "geometry.descriptor.bounds_invalid",
          "LOD bounds must be an object with numeric min/max vec3",
          lod_path + ".bounds");
        co_return std::nullopt;
      }

      const auto mesh_type = lod_doc.at("mesh_type").get<std::string>();
      auto mesh_desc = data::pak::geometry::MeshDesc {};
      const auto lod_name = lod_doc.at("name").get<std::string>();
      ValidateAndCopyName(context.session, context.request, lod_name,
        mesh_desc.name, std::size(mesh_desc.name),
        "geometry.descriptor.lod_name_truncated",
        "LOD name truncated to fit descriptor limit", lod_path + ".name");

      auto mesh_bindings = MeshBufferBindings {};
      std::vector<std::byte> procedural_blob;
      std::optional<ResolvedBufferSidecar> resolved_vb {};
      std::optional<ResolvedBufferSidecar> resolved_ib {};
      uint32_t procedural_vertex_count = 0;
      uint32_t procedural_index_count = 0;

      if (mesh_type == "standard" || mesh_type == "skinned") {
        const auto& refs = lod_doc.at("buffers");
        const auto vb_ref = refs.at("vb_ref").get<std::string>();
        const auto ib_ref = refs.at("ib_ref").get<std::string>();

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

        if (mesh_type == "skinned") {
          mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kSkinned);
          const auto& skin = lod_doc.at("skinning");

          auto joint_index_sidecar = co_await ResolveBufferSidecarByVirtualPath(
            context, skin.at("joint_index_ref").get<std::string>(),
            lod_path + ".skinning.joint_index_ref");
          auto joint_weight_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skin.at("joint_weight_ref").get<std::string>(),
              lod_path + ".skinning.joint_weight_ref");
          auto inverse_bind_sidecar
            = co_await ResolveBufferSidecarByVirtualPath(context,
              skin.at("inverse_bind_ref").get<std::string>(),
              lod_path + ".skinning.inverse_bind_ref");
          auto joint_remap_sidecar = co_await ResolveBufferSidecarByVirtualPath(
            context, skin.at("joint_remap_ref").get<std::string>(),
            lod_path + ".skinning.joint_remap_ref");
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

          mesh_desc.info.skinned.vertex_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.index_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.joint_index_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.joint_weight_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.inverse_bind_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.joint_remap_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.skinned.joint_count
            = lod_doc.at("skinning").at("joint_count").get<uint16_t>();
          mesh_desc.info.skinned.influences_per_vertex
            = lod_doc.at("skinning")
                .at("influences_per_vertex")
                .get<uint16_t>();
          mesh_desc.info.skinned.flags
            = lod_doc.at("skinning").contains("flags")
            ? lod_doc.at("skinning").at("flags").get<uint32_t>()
            : 0U;
          if (lod_doc.at("skinning").contains("skeleton_ref")) {
            const auto skeleton_ref
              = lod_doc.at("skinning").at("skeleton_ref").get<std::string>();
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
          std::copy_n(
            lod_bounds.min.data(), 3, mesh_desc.info.skinned.bounding_box_min);
          std::copy_n(
            lod_bounds.max.data(), 3, mesh_desc.info.skinned.bounding_box_max);
        } else {
          mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kStandard);
          mesh_desc.info.standard.vertex_buffer
            = data::pak::core::kNoResourceIndex;
          mesh_desc.info.standard.index_buffer
            = data::pak::core::kNoResourceIndex;
          std::copy_n(
            lod_bounds.min.data(), 3, mesh_desc.info.standard.bounding_box_min);
          std::copy_n(
            lod_bounds.max.data(), 3, mesh_desc.info.standard.bounding_box_max);
        }
      } else {
        mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kProcedural);
        auto procedural_name = std::string {};
        if (!BuildProceduralParamBlob(lod_doc, lod_path + ".procedural",
              procedural_name, procedural_blob)) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "geometry.procedural.params_invalid",
            "Failed encoding procedural parameter blob",
            lod_path + ".procedural.params");
          co_return std::nullopt;
        }
        ValidateAndCopyName(context.session, context.request, procedural_name,
          mesh_desc.name, std::size(mesh_desc.name),
          "geometry.procedural.name_truncated",
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

      const auto& submeshes_doc = lod_doc.at("submeshes");
      mesh_desc.submesh_count = static_cast<uint32_t>(submeshes_doc.size());
      uint64_t total_mesh_views = 0;
      for (const auto& submesh_doc : submeshes_doc) {
        total_mesh_views += submesh_doc.at("views").size();
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

      if (mesh_desc.IsProcedural()) {
        if (!procedural_blob.empty()) {
          if (!writer.WriteBlob(std::as_bytes(std::span<const std::byte>(
                procedural_blob.data(), procedural_blob.size())))) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError, "geometry.descriptor.serialize_failed",
              "Failed writing procedural parameter blob",
              lod_path + ".procedural.params");
            co_return std::nullopt;
          }
        }
      }

      for (size_t submesh_i = 0; submesh_i < submeshes_doc.size();
        ++submesh_i) {
        const auto& submesh_doc = submeshes_doc.at(submesh_i);
        const auto submesh_path
          = lod_path + ".submeshes[" + std::to_string(submesh_i) + "]";
        auto submesh_desc = data::pak::geometry::SubMeshDesc {};

        const auto submesh_name = submesh_doc.contains("name")
          ? submesh_doc.at("name").get<std::string>()
          : ("submesh_" + std::to_string(submesh_i));
        ValidateAndCopyName(context.session, context.request, submesh_name,
          submesh_desc.name, std::size(submesh_desc.name),
          "geometry.descriptor.submesh_name_truncated",
          "Submesh name truncated to fit descriptor limit",
          submesh_path + ".name");

        const auto material_ref
          = submesh_doc.at("material_ref").get<std::string>();
        const auto material_key = ResolveMaterialKeyByVirtualPath(
          context, material_ref, submesh_path + ".material_ref");
        if (!material_key.has_value()) {
          co_return std::nullopt;
        }
        submesh_desc.material_asset_key = *material_key;
        submesh_desc.mesh_view_count
          = static_cast<uint32_t>(submesh_doc.at("views").size());

        auto submesh_bounds = lod_bounds;
        if (submesh_doc.contains("bounds")) {
          if (!ParseBounds(submesh_doc.at("bounds"), submesh_bounds)) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError, "geometry.descriptor.bounds_invalid",
              "Submesh bounds must be an object with numeric min/max vec3",
              submesh_path + ".bounds");
            co_return std::nullopt;
          }
        }

        std::copy_n(
          submesh_bounds.min.data(), 3, submesh_desc.bounding_box_min);
        std::copy_n(
          submesh_bounds.max.data(), 3, submesh_desc.bounding_box_max);

        if (!WritePod(writer, submesh_desc)) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError, "geometry.descriptor.serialize_failed",
            "Failed writing submesh descriptor", submesh_path);
          co_return std::nullopt;
        }

        for (size_t view_i = 0; view_i < submesh_doc.at("views").size();
          ++view_i) {
          const auto& view_doc = submesh_doc.at("views").at(view_i);
          const auto view_path
            = submesh_path + ".views[" + std::to_string(view_i) + "]";
          auto view_desc = data::pak::geometry::MeshViewDesc {};

          if (mesh_desc.IsProcedural()) {
            const auto view_ref = view_doc.at("view_ref").get<std::string>();
            if (view_ref != internal::kImplicitBufferViewName) {
              AddDiagnostic(context.session, context.request,
                ImportSeverity::kError, "geometry.procedural.view_ref_invalid",
                "Procedural submesh view_ref must be '__all__'",
                view_path + ".view_ref");
              co_return std::nullopt;
            }
            view_desc.first_index = 0;
            view_desc.index_count = procedural_index_count;
            view_desc.first_vertex = 0;
            view_desc.vertex_count = procedural_vertex_count;
          } else {
            if (!resolved_vb.has_value() || !resolved_ib.has_value()) {
              co_return std::nullopt;
            }

            const auto view_ref = view_doc.at("view_ref").get<std::string>();
            if (!ResolveMeshViewPair(context, *resolved_vb, *resolved_ib,
                  view_ref, view_path, view_desc)) {
              co_return std::nullopt;
            }
          }

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
  auto session = ImportSession(Request(), FileReader(), FileWriter(),
    ThreadPool(), TableRegistry(), IndexRegistry());

  if (!Request().geometry_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "geometry.descriptor.request_invalid",
      "GeometryDescriptorImportJob requires request geometry_descriptor "
      "payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid geometry descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = nlohmann::json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = nlohmann::json::parse(
      Request().geometry_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }

  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "geometry.descriptor.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid geometry descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "geometry.descriptor.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid geometry descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Geometry descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = GeometryExecutionContext {
    .session = session,
    .request = Request(),
    .reader = FileReader(),
    .mounts = {},
    .buffer_cache = {},
    .material_cache = {},
  };
  LoadMountedInspections(context);

  if (descriptor_doc.contains("buffers")) {
    auto pipeline = BufferPipeline(*ThreadPool(),
      BufferPipeline::Config {
        .queue_capacity = Concurrency().buffer.queue_capacity,
        .worker_count = Concurrency().buffer.workers,
        .with_content_hashing = EffectiveContentHashingEnabled(
          Request().options.with_content_hashing),
      });
    StartPipeline(pipeline);

    auto submitter
      = BufferImportSubmitter(session, Request(), FileReader(), StopToken());
    const auto chunks = BuildSubmitterBufferChunks(descriptor_doc);
    const auto submission = co_await submitter.SubmitBufferChunks(
      chunks, Request().source_path.parent_path(), pipeline, "buffers");
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

  const auto prepared_opt
    = co_await PrepareGeometryDescriptor(context, descriptor_doc);
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
  const auto finalized_bytes = co_await finalizer.FinalizeDescriptorBytes(
    prepared.lod_bindings, prepared.descriptor_bytes,
    std::span<const GeometryPipeline::MaterialKeyPatch> {},
    finalize_diagnostics);
  AddDiagnostics(session, std::move(finalize_diagnostics));

  if (!finalized_bytes.has_value()) {
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
    virtual_path, descriptor_relpath, *finalized_bytes);
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto GeometryDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
