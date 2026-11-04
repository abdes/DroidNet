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
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <glm/common.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/GeometryDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/GeometryPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MeshBufferRules.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import::detail {

namespace {

  using JsonPointer = nlohmann::json::json_pointer;

  // Copying the sidecar strings and maps can only fail on allocation.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct ResolvedBufferSidecar final {
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    std::unordered_map<std::string, data::BufferResourceView> views;
    //! Where the bytes of a buffer an earlier cook emitted are stored.
    std::optional<cooked::StoredBytes> stored;
    //! The bytes, once read or when this job cooked the buffer.
    std::shared_ptr<const std::vector<std::byte>> contents;
  };

  //! Borrowed for the duration of one job execution, which owns both.
  struct GeometryExecutionContext final {
    ImportSession& session; // NOLINT(*-avoid-const-or-ref-data-members)
    const ImportRequest& request; // NOLINT(*-avoid-const-or-ref-data-members)
    observer_ptr<IAsyncFileReader> reader;
    std::vector<internal::MountedCookedSource> mounts;
    std::unordered_map<std::string, ResolvedBufferSidecar> buffer_cache;
    std::unordered_map<std::string, data::AssetKey> asset_cache;
  };

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    const JsonPointer& pointer = JsonPointer {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = pointer.to_string(),
    });
  }

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
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

  auto BuildViewMap(const std::span<const data::BufferResourceView> views)
    -> std::unordered_map<std::string, data::BufferResourceView>
  {
    auto map = std::unordered_map<std::string, data::BufferResourceView> {};
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
          .stored = std::nullopt,
          .contents = emitted.contents,
        });
    }
  }

  //! Resolves a reference read in the geometry descriptor.
  /*!
   Reports `import.reference_invalid` and returns nothing for text the
   grammar refuses.
  */
  auto ResolveReferenceText(GeometryExecutionContext& context,
    const std::string_view text, const JsonPointer& pointer)
    -> std::optional<internal::ResolvedReference>
  {
    auto reference = internal::ResolveJobReference(context.request, text);
    if (!reference.has_value()) {
      context.session.AddDiagnostic(internal::RefuseReference(
        context.request, text, reference.error(), pointer.to_string()));
      return std::nullopt;
    }
    return std::move(*reference);
  }

  // The caller awaits each resolution inline while it owns `context`.
  auto ResolveBufferSidecar(GeometryExecutionContext& context,
    const std::string_view text, const JsonPointer& pointer)
    -> co::Co<std::optional<ResolvedBufferSidecar>>
  {
    const auto reference = ResolveReferenceText(context, text, pointer);
    if (!reference.has_value()) {
      co_return std::nullopt;
    }

    if (const auto it = context.buffer_cache.find(reference->resolved);
      it != context.buffer_cache.end()) {
      co_return it->second;
    }

    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          context.request, reference->resolved, relpath)) {
      context.session.AddDiagnostic(internal::RefuseUnmountedReference(
        context.request, *reference, pointer.to_string()));
      co_return std::nullopt;
    }

    if (context.reader == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryBufferReaderUnavailable),
        "Async file reader is not available", pointer);
      co_return std::nullopt;
    }

    auto matches
      = std::vector<observer_ptr<const internal::MountedCookedSource>> {};
    auto looked_up = std::vector<std::filesystem::path> {};
    // Only an index lists a sidecar: a descriptor file it does not list is
    // none.
    for (auto& mount : std::views::reverse(context.mounts)) {
      if (!mount.content.has_value()) {
        continue;
      }
      if (mount.content->FindSidecar(reference->resolved) == nullptr) {
        looked_up.push_back(mount.root / std::filesystem::path(relpath));
        continue;
      }
      matches.emplace_back(&mount);
    }

    if (matches.size() > 1U) {
      auto message = std::string {
        "Buffer reference resolved to multiple mounted "
        "sidecars; provide a single canonical source: "
      } + reference->resolved;
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryBufferSidecarAmbiguous),
        std::move(message), pointer);
      co_return std::nullopt;
    }

    if (!matches.empty()) {
      const auto& matched = *matches.front();
      if (base::PathIdentityKey(std::filesystem::weakly_canonical(matched.root))
        != base::PathIdentityKey(
          std::filesystem::weakly_canonical(context.session.CookedRoot()))) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          std::string(import_codes::kGeometryBufferForeignRoot),
          "Raw geometry buffers must belong to the geometry's cooked root. "
          "Reference the other root's geometry asset, or import the buffer "
          "source into this root.",
          pointer);
        co_return std::nullopt;
      }
      // The file its index lists, under its content name.
      const auto* sidecar = matched.content.has_value()
        ? matched.content->FindSidecar(reference->resolved)
        : nullptr;
      CHECK_NOTNULL_F(sidecar, "A matched cooked root lists the sidecar");
      const auto descriptor_path = sidecar->stored.file;
      const auto read_start = std::chrono::steady_clock::now();
      const auto read_result
        = co_await context.reader->ReadFile(descriptor_path);
      context.session.AddLoadDuration(
        MakeDuration(read_start, std::chrono::steady_clock::now()));
      if (!read_result.has_value()) {
        if (auto too_large = SourceTooLargeDiagnostic(read_result.error(),
              descriptor_path, context.reader->SourceLimit())) {
          context.session.AddDiagnostic(std::move(*too_large));
        } else {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError,
            std::string(import_codes::kGeometryBufferSidecarReadFailed),
            "Failed reading buffer descriptor '" + descriptor_path.string()
              + "': " + read_result.error().message(),
            pointer);
        }
        co_return std::nullopt;
      }

      const auto decoded
        = data::DecodeBufferResourceDescriptor(read_result.value().Bytes());
      if (!decoded) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          std::string(import_codes::kGeometryBufferSidecarInvalid),
          decoded.error().what(), pointer);
        co_return std::nullopt;
      }

      const auto* buffer_table = matched.content.has_value()
        ? &matched.content->ResourceTable(cooked::TableKind::kBuffer)
        : nullptr;
      CHECK_NOTNULL_F(buffer_table, "A matched cooked root has a buffer table");
      auto stored = std::optional<cooked::StoredBytes> {};
      try {
        stored = buffer_table->PayloadStored(decoded->index);
      } catch (const std::out_of_range& error) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          std::string(import_codes::kGeometryBufferSidecarInvalid),
          error.what(), pointer);
        co_return std::nullopt;
      }

      auto resolved = ResolvedBufferSidecar {
        .resource_index = decoded->index,
        .descriptor = decoded->descriptor,
        .views = BuildViewMap(decoded->views),
        .stored = std::move(stored),
        .contents = nullptr,
      };
      context.buffer_cache.insert_or_assign(reference->resolved, resolved);
      co_return resolved;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      std::string(import_codes::kGeometryBufferSidecarMissing),
      "Buffer descriptor was not found: "
        + internal::DescribeLookup(*reference,
          internal::AssetReferenceRefused {
            .reason = internal::AssetReferenceRefusal::kMissing,
            .found_type = std::nullopt,
            .looked_up = std::move(looked_up),
          }),
      pointer);
    co_return std::nullopt;
  }

  //! The sidecar `text` names, with the bytes of its buffer.
  /*!
   A buffer this job cooked holds the bytes `ContentsTheLinkReads` kept; one an
   earlier cook emitted is read once from the cooked root.
  */
  // The caller awaits each resolution inline while it owns `context`.
  auto ResolveBufferWithContents(GeometryExecutionContext& context,
    const std::string_view text, const JsonPointer& pointer)
    -> co::Co<std::optional<ResolvedBufferSidecar>>
  {
    auto sidecar = co_await ResolveBufferSidecar(context, text, pointer);
    if (!sidecar.has_value() || sidecar->contents != nullptr) {
      co_return sidecar;
    }
    const auto failed = [&](const std::string& why) -> void {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryBufferContentsReadFailed),
        "Failed reading the bytes of buffer '" + std::string(text)
          + "': " + why,
        pointer);
    };
    if (!sidecar->stored.has_value()) {
      failed("this job did not keep them");
      co_return std::nullopt;
    }
    const auto& stored = *sidecar->stored;
    const auto size = stored.range.count.get();
    if (size != sidecar->descriptor.size_bytes || size == 0U) {
      failed("the cooked root stores " + std::to_string(size)
        + " bytes for a sidecar of "
        + std::to_string(sidecar->descriptor.size_bytes));
      co_return std::nullopt;
    }
    const auto read_start = std::chrono::steady_clock::now();
    const auto bytes = co_await context.reader->ReadFile(stored.file,
      ReadOptions { .offset = stored.range.offset.get(), .max_bytes = size });
    context.session.AddLoadDuration(
      MakeDuration(read_start, std::chrono::steady_clock::now()));
    if (!bytes.has_value()) {
      failed(bytes.error().message());
      co_return std::nullopt;
    }
    const auto read = bytes.value().Bytes();
    if (read.size() != size) {
      failed("the cooked root holds " + std::to_string(read.size()) + " of its "
        + std::to_string(size) + " bytes");
      co_return std::nullopt;
    }
    sidecar->contents = std::make_shared<const std::vector<std::byte>>(
      read.begin(), read.end());
    const auto reference = internal::ResolveJobReference(context.request, text);
    if (reference.has_value()) {
      context.buffer_cache.insert_or_assign(reference->resolved, *sidecar);
    }
    co_return sidecar;
  }

  //! The products whose bytes the link reads: vertex, index and joint index
  //! buffers.
  auto ContentsTheLinkReads(const ImportRequest& request,
    const internal::GeometrySource& source) -> std::unordered_set<std::string>
  {
    auto products = std::unordered_set<std::string> {};
    const auto add = [&](const std::string_view text) -> void {
      // A refused reference is reported when the link resolves it.
      if (const auto reference = internal::ResolveJobReference(request, text);
        reference.has_value()) {
        products.insert(reference->resolved);
      }
    };
    for (const auto& lod : source.lods) {
      if (const auto* buffers
        = std::get_if<internal::GeometrySource::Buffers>(&lod.mesh)) {
        add(buffers->vertex);
        add(buffers->index);
      } else if (const auto* skinned
        = std::get_if<internal::GeometrySource::Skinned>(&lod.mesh)) {
        add(skinned->buffers.vertex);
        add(skinned->buffers.index);
        add(skinned->joint_index);
      }
    }
    return products;
  }

  //! The diagnostic codes a family of asset references reports its lookup
  //! refusals with, and what the family's descriptor is called in them.
  struct AssetReferenceCodes final {
    std::string_view missing;
    //! Empty for a family that accepts an asset of any type.
    std::string_view type_mismatch;
    std::string_view noun;
  };

  //! The key of the asset a reference in the descriptor names.
  /*!
   The mounted indexes are searched by `internal::LookupReference`;
   a refusal is reported with the family's `codes`, or as
   `import.reference_unmounted` for a reference in no mounted cooked root.
  */
  auto LookupAssetKey(GeometryExecutionContext& context,
    const std::string_view text, const JsonPointer& pointer,
    const std::optional<data::AssetType> expected_type,
    const AssetReferenceCodes& codes) -> std::optional<data::AssetKey>
  {
    const auto reference = ResolveReferenceText(context, text, pointer);
    if (!reference.has_value()) {
      return std::nullopt;
    }
    if (const auto it = context.asset_cache.find(reference->resolved);
      it != context.asset_cache.end()) {
      return it->second;
    }

    const auto found = internal::LookupReference(
      context.mounts, context.request, *reference, expected_type);
    if (found.has_value()) {
      context.asset_cache.insert_or_assign(reference->resolved, found->key);
      return found->key;
    }
    const auto& refused = found.error();
    switch (refused.reason) {
    case internal::AssetReferenceRefusal::kUnmounted:
      context.session.AddDiagnostic(internal::RefuseUnmountedReference(
        context.request, *reference, pointer.to_string()));
      break;
    case internal::AssetReferenceRefusal::kMissing:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(codes.missing),
        std::string(codes.noun)
          + " was not found: " + internal::DescribeLookup(*reference, refused),
        pointer);
      break;
    case internal::AssetReferenceRefusal::kTypeMismatch:
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(codes.type_mismatch),
        std::string(codes.noun) + " names a "
          + std::string(data::to_string(
            refused.found_type.value_or(data::AssetType::kUnknown)))
          + " asset instead: " + internal::DescribeLookup(*reference, refused),
        pointer);
      break;
    }
    return std::nullopt;
  }

  auto CheckedU32(const uint64_t value, ImportSession& session,
    const ImportRequest& request, std::string_view code,
    std::string_view message, const JsonPointer& pointer)
    -> std::optional<uint32_t>
  {
    if (value > std::numeric_limits<uint32_t>::max()) {
      AddDiagnostic(session, request, ImportSeverity::kError, std::string(code),
        std::string(message), pointer);
      return std::nullopt;
    }
    return static_cast<uint32_t>(value);
  }

  auto FindBufferView(const ResolvedBufferSidecar& sidecar,
    std::string_view view_name) -> const data::BufferResourceView*
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
    const JsonPointer& view_pointer,
    data::pak::geometry::MeshViewDesc& out_view_desc) -> bool
  {
    const auto* const vb_view = FindBufferView(vb_sidecar, view_ref);
    if (vb_view == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryBufferViewMissing),
        "Vertex buffer view_ref was not found: " + std::string(view_ref),
        view_pointer / "view_ref");
      return false;
    }

    const auto* const ib_view = FindBufferView(ib_sidecar, view_ref);
    if (ib_view == nullptr) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryBufferViewMissing),
        "Index buffer view_ref was not found: " + std::string(view_ref),
        view_pointer / "view_ref");
      return false;
    }

    const auto first_index
      = CheckedU32(ib_view->element_offset, context.session, context.request,
        import_codes::kGeometryBufferViewOverflow,
        "Mesh view first_index exceeds supported range",
        view_pointer / "view_ref");
    const auto index_count = CheckedU32(ib_view->element_count, context.session,
      context.request, import_codes::kGeometryBufferViewOverflow,
      "Mesh view index_count exceeds supported range",
      view_pointer / "view_ref");
    const auto first_vertex
      = CheckedU32(vb_view->element_offset, context.session, context.request,
        import_codes::kGeometryBufferViewOverflow,
        "Mesh view first_vertex exceeds supported range",
        view_pointer / "view_ref");
    const auto vertex_count
      = CheckedU32(vb_view->element_count, context.session, context.request,
        import_codes::kGeometryBufferViewOverflow,
        "Mesh view vertex_count exceeds supported range",
        view_pointer / "view_ref");

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
    const JsonPointer& pointer, std::string_view logical_name) -> bool
  {
    const auto usage_flags = static_cast<data::BufferResource::UsageFlags>(
      sidecar.descriptor.usage_flags);
    if ((usage_flags & required_flag) == required_flag) {
      return true;
    }

    AddDiagnostic(context.session, context.request, ImportSeverity::kError,
      std::string(import_codes::kGeometryBufferUsageMismatch),
      std::string(logical_name) + " does not advertise required usage flag",
      pointer);
    return false;
  }

  //! Runs a `Data` buffer rule, reporting its refusal as `code` at `pointer`.
  template <typename Rule>
  auto PassesBufferRule(GeometryExecutionContext& context, const Rule& rule,
    const std::string_view code, const JsonPointer& pointer) -> bool
  {
    try {
      rule();
      return true;
    } catch (const data::CookedFormatError& error) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(code), error.what(), pointer);
      return false;
    }
  }

  //! The number of indices of `sidecar`'s type its bytes hold, read by the
  //! same rule the runtime binds the buffer by.
  auto IndicesOf(const ResolvedBufferSidecar& sidecar)
    -> data::detail::IndexBufferView
  {
    return data::detail::IndexBufferView {
      .bytes = *sidecar.contents,
      .type = data::IndexTypeOf(
        static_cast<Format>(sidecar.descriptor.element_format),
        sidecar.descriptor.element_stride),
    };
  }

  //! Refuses a vertex buffer of other records than `data::Vertex`, or of
  //! values the renderer refuses.
  auto PassesVertexBufferRule(GeometryExecutionContext& context,
    const ResolvedBufferSidecar& vertices, const JsonPointer& pointer) -> bool
  {
    return PassesBufferRule(
             context,
             [&vertices] -> void {
               data::CheckVertexBufferRow(vertices.descriptor);
             },
             import_codes::kGeometryBufferLayoutInvalid, pointer)
      && PassesBufferRule(
        context,
        [&vertices] -> void { data::CheckVertexValues(*vertices.contents); },
        import_codes::kGeometryBufferVerticesInvalid, pointer);
  }

  //! Refuses an index buffer of other values than 16-bit or 32-bit indices.
  auto PassesIndexBufferRule(GeometryExecutionContext& context,
    const ResolvedBufferSidecar& indices, const JsonPointer& pointer) -> bool
  {
    return PassesBufferRule(
      context, [&indices] -> void { static_cast<void>(IndicesOf(indices)); },
      import_codes::kGeometryBufferLayoutInvalid, pointer);
  }

  //! Refuses a view its buffers cannot draw, which the runtime refuses when it
  //! binds them: a range past a buffer, or an index past the view's vertices.
  auto PassesMeshViewRule(GeometryExecutionContext& context,
    const data::pak::geometry::MeshViewDesc& view,
    const ResolvedBufferSidecar& vertices, const ResolvedBufferSidecar& indices,
    const JsonPointer& view_pointer) -> bool
  {
    return PassesBufferRule(
      context,
      [&] -> void {
        data::CheckMeshViewRange(
          data::MeshViewRange {
            .first_vertex = view.first_vertex,
            .vertex_count = view.vertex_count,
            .first_index = view.first_index,
            .index_count = view.index_count,
          },
          vertices.descriptor.size_bytes / sizeof(data::Vertex),
          IndicesOf(indices));
      },
      import_codes::kGeometryBufferViewInvalid, view_pointer / "view_ref");
  }

  using Bounds = internal::GeometrySource::Bounds;

  //! One LOD's mesh as its descriptor records it: a procedural mesh is
  //! generated once, its counts sizing its views and its vertices bounding it.
  struct LodGeometry final {
    Bounds bounds;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
  };

  //! The bounds of `vertices`, which are not empty.
  auto BoundsOf(std::span<const data::Vertex> vertices) -> Bounds
  {
    auto low = vertices.front().position;
    auto high = low;
    for (const auto& vertex : vertices) {
      low = glm::min(low, vertex.position);
      high = glm::max(high, vertex.position);
    }
    return {
      .min = { low.x, low.y, low.z },
      .max = { high.x, high.y, high.z },
    };
  }

  auto GenerateLod(GeometryExecutionContext& context,
    const internal::GeometrySource::Procedural& procedural,
    const JsonPointer& lod_pointer) -> std::optional<LodGeometry>
  {
    const auto generated = data::GenerateMeshBuffers(
      procedural.name, std::span<const std::byte>(procedural.parameters));
    if (!generated.has_value() || generated->first.empty()) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryProceduralGenerationFailed),
        "Procedural mesh generator rejected descriptor parameters",
        lod_pointer / "procedural");
      return std::nullopt;
    }
    const auto& [vertices, indices] = *generated;
    const auto vertex_count = CheckedU32(vertices.size(), context.session,
      context.request, import_codes::kGeometryProceduralMeshTooLarge,
      "Generated procedural vertex count exceeds supported range",
      lod_pointer / "procedural");
    const auto index_count = CheckedU32(indices.size(), context.session,
      context.request, import_codes::kGeometryProceduralMeshTooLarge,
      "Generated procedural index count exceeds supported range",
      lod_pointer / "procedural");
    if (!vertex_count.has_value() || !index_count.has_value()) {
      return std::nullopt;
    }
    return LodGeometry {
      .bounds = BoundsOf(vertices),
      .vertex_count = *vertex_count,
      .index_count = *index_count,
    };
  }

  //! Each LOD's geometry: a buffer mesh's authored bounds, or the generated
  //! procedural mesh.
  auto PrepareLods(
    GeometryExecutionContext& context, const internal::GeometrySource& source)
    -> std::optional<std::vector<LodGeometry>>
  {
    auto lods = std::vector<LodGeometry> {};
    lods.reserve(source.lods.size());
    for (size_t lod_i = 0; lod_i < source.lods.size(); ++lod_i) {
      const auto& lod = source.lods.at(lod_i);
      const auto* procedural
        = std::get_if<internal::GeometrySource::Procedural>(&lod.mesh);
      if (procedural == nullptr) {
        // The schema requires a buffer mesh's bounds.
        lods.push_back({ .bounds = lod.bounds.value_or(Bounds {}) });
        continue;
      }
      auto generated
        = GenerateLod(context, *procedural, JsonPointer { "/lods" } / lod_i);
      if (!generated.has_value()) {
        return std::nullopt;
      }
      lods.push_back(*generated);
    }
    return lods;
  }

  //! The geometry's bounds: the authored ones, which must contain every LOD,
  //! else the union of the LOD bounds.
  auto GeometryBounds(GeometryExecutionContext& context,
    const internal::GeometrySource& source, std::span<const LodGeometry> lods)
    -> std::optional<Bounds>
  {
    auto lods_bounds = lods.front().bounds;
    for (const auto& lod : lods) {
      lods_bounds = lods_bounds.Union(lod.bounds);
    }
    if (!source.bounds.has_value()) {
      return lods_bounds;
    }
    if (!source.bounds->Contains(lods_bounds)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryDescriptorBoundsInvalid),
        "Geometry bounds must be ordered and contain every LOD's bounds",
        JsonPointer { "/bounds" });
      return std::nullopt;
    }
    return source.bounds;
  }

  struct LinkedGeometryDescriptor final {
    std::string geometry_name;
    std::vector<std::byte> descriptor_bytes;
    std::vector<MeshBufferBindings> lod_bindings;
  };

  // The caller awaits the link inline while it owns `context` and `source`.
  auto LinkGeometryDescriptor(
    GeometryExecutionContext& context, const internal::GeometrySource& source)
    -> co::Co<std::optional<LinkedGeometryDescriptor>>
  {
    auto prepared = LinkedGeometryDescriptor {};
    prepared.geometry_name = source.name;
    const auto lod_geometry = PrepareLods(context, source);
    if (!lod_geometry.has_value()) {
      co_return std::nullopt;
    }
    const auto root_bounds = GeometryBounds(context, source, *lod_geometry);
    if (!root_bounds.has_value()) {
      co_return std::nullopt;
    }
    const auto lod_count = source.lods.size();

    auto output_stream = serio::MemoryStream {};
    auto writer = serio::Writer(output_stream);
    const auto pack = writer.ScopedAlignment(1);

    auto asset_desc = data::pak::geometry::GeometryAssetDesc {};
    asset_desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kGeometry);
    asset_desc.header.version = data::pak::geometry::kGeometryAssetVersion;
    asset_desc.lod_count = static_cast<uint32_t>(lod_count);
    std::ranges::copy(
      root_bounds->min, std::begin(asset_desc.bounding_box_min));
    std::ranges::copy(
      root_bounds->max, std::begin(asset_desc.bounding_box_max));
    const auto surface_table
      = internal::MakeSurfaceTable(source.DeclaredSurfaces(), source.surfaces);
    asset_desc.surface_count = static_cast<uint32_t>(surface_table.size());
    asset_desc.default_lod_policy
      = internal::MakeDefaultLodPolicy(source.lod_policy);
    auto strings = data::DescriptorStringsBuilder {};
    asset_desc.header.name_offset = strings.Add(prepared.geometry_name);

    if (!WritePod(writer, asset_desc)) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryDescriptorSerializeFailed),
        "Failed writing geometry descriptor header");
      co_return std::nullopt;
    }

    prepared.lod_bindings.reserve(lod_count);

    for (size_t lod_i = 0; lod_i < lod_count; ++lod_i) {
      const auto& lod = source.lods.at(lod_i);
      const auto lod_pointer = JsonPointer { "/lods" } / lod_i;
      const auto& geometry = lod_geometry->at(lod_i);
      const auto& lod_bounds = geometry.bounds;
      auto mesh_desc = data::pak::geometry::MeshDesc {};
      mesh_desc.name_offset = strings.Add(lod.name);

      auto mesh_bindings = MeshBufferBindings {};
      std::span<const std::byte> procedural_blob;
      std::optional<ResolvedBufferSidecar> resolved_vb {};
      std::optional<ResolvedBufferSidecar> resolved_ib {};

      const auto* skinned
        = std::get_if<internal::GeometrySource::Skinned>(&lod.mesh);
      const auto* buffers = skinned != nullptr
        ? &skinned->buffers
        : std::get_if<internal::GeometrySource::Buffers>(&lod.mesh);
      if (buffers != nullptr) {
        const auto& vb_ref = buffers->vertex;
        const auto& ib_ref = buffers->index;

        auto vb_sidecar_opt = co_await ResolveBufferWithContents(
          context, vb_ref, lod_pointer / "buffers" / "vb_ref");
        auto ib_sidecar_opt = co_await ResolveBufferWithContents(
          context, ib_ref, lod_pointer / "buffers" / "ib_ref");
        if (!vb_sidecar_opt.has_value() || !ib_sidecar_opt.has_value()) {
          co_return std::nullopt;
        }

        if (!EnsureBufferUsageFlags(context, *vb_sidecar_opt,
              data::BufferResource::UsageFlags::kVertexBuffer,
              lod_pointer / "buffers" / "vb_ref", "Vertex buffer")
          || !EnsureBufferUsageFlags(context, *ib_sidecar_opt,
            data::BufferResource::UsageFlags::kIndexBuffer,
            lod_pointer / "buffers" / "ib_ref", "Index buffer")) {
          co_return std::nullopt;
        }
        if (!PassesVertexBufferRule(
              context, *vb_sidecar_opt, lod_pointer / "buffers" / "vb_ref")
          || !PassesIndexBufferRule(
            context, *ib_sidecar_opt, lod_pointer / "buffers" / "ib_ref")) {
          co_return std::nullopt;
        }

        resolved_vb = *vb_sidecar_opt;
        resolved_ib = *ib_sidecar_opt;
        mesh_bindings.vertex_buffer = vb_sidecar_opt->resource_index;
        mesh_bindings.index_buffer = ib_sidecar_opt->resource_index;

        if (skinned != nullptr) {
          mesh_desc.mesh_type = static_cast<uint8_t>(data::MeshType::kSkinned);

          auto joint_index_sidecar = co_await ResolveBufferWithContents(context,
            skinned->joint_index, lod_pointer / "skinning" / "joint_index_ref");
          auto joint_weight_sidecar
            = co_await ResolveBufferSidecar(context, skinned->joint_weight,
              lod_pointer / "skinning" / "joint_weight_ref");
          auto inverse_bind_sidecar
            = co_await ResolveBufferSidecar(context, skinned->inverse_bind,
              lod_pointer / "skinning" / "inverse_bind_ref");
          auto joint_remap_sidecar = co_await ResolveBufferSidecar(context,
            skinned->joint_remap, lod_pointer / "skinning" / "joint_remap_ref");
          if (!joint_index_sidecar.has_value()
            || !joint_weight_sidecar.has_value()
            || !inverse_bind_sidecar.has_value()
            || !joint_remap_sidecar.has_value()) {
            co_return std::nullopt;
          }
          const auto skin_valid = PassesBufferRule(
            context,
            [&] -> void {
              data::CheckSkinBufferRows(data::SkinBufferRows {
                .joint_indices = joint_index_sidecar->descriptor,
                .joint_weights = joint_weight_sidecar->descriptor,
                .inverse_bind_matrices = inverse_bind_sidecar->descriptor,
                .joint_remap = joint_remap_sidecar->descriptor,
                .vertex_count
                = vb_sidecar_opt->descriptor.size_bytes / sizeof(data::Vertex),
                .joint_count = skinned->joint_count,
                .influences_per_vertex = skinned->influences_per_vertex,
              });
              data::CheckJointIndices(
                *joint_index_sidecar->contents, skinned->joint_count);
            },
            import_codes::kGeometrySkinningBuffersInvalid,
            lod_pointer / "skinning");
          if (!skin_valid) {
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
            const auto skeleton_key = LookupAssetKey(context, skeleton_ref,
              lod_pointer / "skinning" / "skeleton_ref", std::nullopt,
              {
                .missing = import_codes::kGeometryAssetMissing,
                .type_mismatch = {},
                .noun = "Skeleton descriptor",
              });
            if (!skeleton_key.has_value()) {
              AddDiagnostic(context.session, context.request,
                ImportSeverity::kError,
                std::string(import_codes::kGeometrySkinningSkeletonMissing),
                "skeleton_ref could not be resolved",
                lod_pointer / "skinning" / "skeleton_ref");
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
        // A procedural mesh's name is its generator identity.
        mesh_desc.name_offset = strings.Add(procedural_name);

        // Write the whole arm: the union starts as the standard arm, whose
        // defaults would otherwise stay in the procedural arm's reserve.
        mesh_desc.info.procedural = data::pak::geometry::ProceduralMeshInfo {
          .params_size = static_cast<uint32_t>(procedural_blob.size()),
          ._reserved = {},
        };
      }

      const auto& submeshes = lod.submeshes;
      mesh_desc.submesh_count = static_cast<uint32_t>(submeshes.size());
      uint64_t total_mesh_views = 0;
      for (const auto& submesh : submeshes) {
        total_mesh_views += submesh.views.size();
      }
      const auto mesh_view_count = CheckedU32(total_mesh_views, context.session,
        context.request, import_codes::kGeometryDescriptorViewCountOverflow,
        "Mesh view count exceeds supported range", lod_pointer / "submeshes");
      if (!mesh_view_count.has_value()) {
        co_return std::nullopt;
      }
      mesh_desc.mesh_view_count = *mesh_view_count;

      if (!WritePod(writer, mesh_desc)) {
        AddDiagnostic(context.session, context.request, ImportSeverity::kError,
          std::string(import_codes::kGeometryDescriptorSerializeFailed),
          "Failed writing mesh descriptor", lod_pointer);
        co_return std::nullopt;
      }

      if (mesh_desc.IsProcedural() && (!procedural_blob.empty())) {
        if (!writer.WriteBlob(std::as_bytes(std::span<const std::byte>(
              procedural_blob.data(), procedural_blob.size())))) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError,
            std::string(import_codes::kGeometryDescriptorSerializeFailed),
            "Failed writing procedural parameter blob",
            lod_pointer / "procedural" / "params");
          co_return std::nullopt;
        }
      }

      for (size_t submesh_i = 0; submesh_i < submeshes.size(); ++submesh_i) {
        const auto& submesh = submeshes.at(submesh_i);
        const auto submesh_pointer = lod_pointer / "submeshes" / submesh_i;
        auto submesh_desc = data::pak::geometry::SubMeshDesc {};
        submesh_desc.surface_id = submesh.surface_id;
        const auto& submesh_name = submesh.name;
        submesh_desc.name_offset = strings.Add(submesh_name);

        const auto& material_ref = submesh.material;
        const auto material_key = LookupAssetKey(context, material_ref,
          submesh_pointer / "material_ref", data::AssetType::kMaterial,
          {
            .missing = import_codes::kGeometryMaterialMissing,
            .type_mismatch = import_codes::kGeometryMaterialTypeMismatch,
            .noun = "Material descriptor",
          });
        if (!material_key.has_value()) {
          co_return std::nullopt;
        }
        submesh_desc.material_asset_key = *material_key;
        submesh_desc.mesh_view_count
          = static_cast<uint32_t>(submesh.views.size());

        const auto submesh_bounds = submesh.bounds.value_or(lod_bounds);

        std::ranges::copy(
          submesh_bounds.min, std::begin(submesh_desc.bounding_box_min));
        std::ranges::copy(
          submesh_bounds.max, std::begin(submesh_desc.bounding_box_max));

        if (!WritePod(writer, submesh_desc)) {
          AddDiagnostic(context.session, context.request,
            ImportSeverity::kError,
            std::string(import_codes::kGeometryDescriptorSerializeFailed),
            "Failed writing submesh descriptor", submesh_pointer);
          co_return std::nullopt;
        }

        for (size_t view_i = 0; view_i < submesh.views.size(); ++view_i) {
          const auto& view = submesh.views.at(view_i);
          const auto view_pointer = submesh_pointer / "views" / view_i;
          auto view_desc = data::pak::geometry::MeshViewDesc {};

          if (mesh_desc.IsProcedural()) {
            view_desc.first_index = 0;
            view_desc.index_count = geometry.index_count;
            view_desc.first_vertex = 0;
            view_desc.vertex_count = geometry.vertex_count;
          } else {
            if (!resolved_vb.has_value() || !resolved_ib.has_value()) {
              co_return std::nullopt;
            }

            if (!ResolveMeshViewPair(context, *resolved_vb, *resolved_ib,
                  view.view_ref, view_pointer, view_desc)
              || !PassesMeshViewRule(
                context, view_desc, *resolved_vb, *resolved_ib, view_pointer)) {
              co_return std::nullopt;
            }
          }
          const auto view_bounds = view.bounds.value_or(submesh_bounds);
          std::ranges::copy(
            view_bounds.min, std::begin(view_desc.bounding_box_min));
          std::ranges::copy(
            view_bounds.max, std::begin(view_desc.bounding_box_max));

          if (!WritePod(writer, view_desc)) {
            AddDiagnostic(context.session, context.request,
              ImportSeverity::kError,
              std::string(import_codes::kGeometryDescriptorSerializeFailed),
              "Failed writing mesh view descriptor", view_pointer);
            co_return std::nullopt;
          }
        }
      }

      prepared.lod_bindings.push_back(mesh_bindings);
    }

    if (!writer.WriteBlob(std::as_bytes(std::span(surface_table)))) {
      AddDiagnostic(context.session, context.request, ImportSeverity::kError,
        std::string(import_codes::kGeometryDescriptorSerializeFailed),
        "Failed writing the surface table", JsonPointer { "/surfaces" });
      co_return std::nullopt;
    }

    const auto bytes = output_stream.Data();
    prepared.descriptor_bytes.assign(bytes.begin(), bytes.end());
    data::AppendDescriptorStrings(prepared.descriptor_bytes, strings);
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

  const auto& descriptor = Request().geometry_descriptor;
  if (!descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      std::string(import_codes::kGeometryDescriptorRequestInvalid),
      "GeometryDescriptorImportJob requires request geometry_descriptor "
      "payload");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid geometry descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  CHECK_NOTNULL_F(Request().source_mounts.get(),
    "A geometry descriptor locates its buffers through the job's mounts");
  const auto source = internal::GeometrySource::FromDescriptor(
    descriptor->descriptor_json, Request().source_path,
    Request().OutputIdentity(), *Request().source_mounts, diagnostics);
  AddDiagnostics(session, std::move(diagnostics));
  if (!source.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor preparation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  auto placement
    = internal::PlaceAsset(Request(), data::kGeometryDescriptorExtension);
  if (!placement.has_value()) {
    session.AddDiagnostic(std::move(placement.error()));
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry virtual path refused");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto context = GeometryExecutionContext {
    .session = session,
    .request = Request(),
    .reader = CookedReader(),
    .mounts = {},
    .buffer_cache = {},
    .asset_cache = {},
  };
  context.mounts = internal::LoadMountedCookedSources(
    session, Request(), import_codes::kGeometryDescriptorIndexLoadFailed);

  if (!source->buffers.empty()) {
    auto& pipeline = CreatePipeline<BufferPipeline>(*ThreadPool(),
      BufferPipeline::Config {
        .queue_capacity = Concurrency().buffer.queue_capacity,
        .worker_count = Concurrency().buffer.workers,
      });

    auto submitter
      = BufferImportSubmitter(session, Request(), FileReader(), StopToken());
    const auto cooked = co_await submitter.CookBuffers(
      source->buffers, ContentsTheLinkReads(Request(), *source), pipeline);

    if (cooked.submitted_count == 0U) {
      if (!session.HasErrors()) {
        AddDiagnostic(session, Request(), ImportSeverity::kError,
          std::string(import_codes::kGeometryBufferNoSubmissions),
          "No buffer work items were submitted");
      }
      ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
        "Geometry descriptor buffer preparation failed");
      co_return co_await FinalizeWithTelemetry(session);
    }

    CacheLocalBufferResults(context, cooked.emitted);
  }

  const auto prepared_opt = co_await LinkGeometryDescriptor(context, *source);
  if (!prepared_opt.has_value()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Geometry descriptor build failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto& prepared = *prepared_opt;
  auto finalizer = GeometryPipeline(GeometryPipeline::Config {});
  auto finalize_diagnostics = std::vector<ImportDiagnostic> {};
  auto finalized = finalizer.FinalizeDescriptor(prepared.lod_bindings,
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

  const auto& [virtual_path, descriptor_relpath] = *placement;
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
auto GeometryDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
