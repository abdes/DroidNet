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
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/base.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/Cooker/Import/Internal/InputSource.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/ModelSettings.h>
#include <Oxygen/Cooker/Import/Internal/ModelTextureSource.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/ScriptSource.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/SourceAnalysisRunner.h>
#include <Oxygen/Cooker/Import/Internal/TextureSourceAssembly_internal.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Core/Version.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Error.h>

namespace oxygen::content::import {
namespace {
  using Kind = ImportDependencyKind;
  using Pointer = nlohmann::json::json_pointer;

  auto PathText(const std::filesystem::path& path) -> std::string
  {
    const auto encoded = path.generic_u8string();
    return { encoded.begin(), encoded.end() };
  }

  auto KindName(const Kind kind) -> std::string_view
  {
    switch (kind) {
    case Kind::kAsset:
      return "asset";
    case Kind::kMaterial:
      return "material";
    case Kind::kGeometry:
      return "geometry";
    case Kind::kScene:
      return "scene";
    case Kind::kTexture:
      return "texture";
    case Kind::kBuffer:
      return "buffer";
    case Kind::kScript:
      return "script";
    case Kind::kInputAction:
      return "input-action";
    case Kind::kInputMappingContext:
      return "input-mapping-context";
    case Kind::kPhysicsScene:
      return "physics-scene";
    }
    throw std::logic_error("Unknown import dependency kind");
  }

  auto AssetKind(const std::optional<data::AssetType> type) -> Kind
  {
    if (!type.has_value()) {
      return Kind::kAsset;
    }
    switch (*type) {
    case data::AssetType::kMaterial:
      return Kind::kMaterial;
    case data::AssetType::kGeometry:
      return Kind::kGeometry;
    case data::AssetType::kScene:
      return Kind::kScene;
    case data::AssetType::kScript:
      return Kind::kScript;
    case data::AssetType::kInputAction:
      return Kind::kInputAction;
    case data::AssetType::kInputMappingContext:
      return Kind::kInputMappingContext;
    case data::AssetType::kPhysicsScene:
      return Kind::kPhysicsScene;
    default:
      return Kind::kAsset;
    }
  }

  auto AppendDiagnostics(std::vector<ImportDiagnostic>& destination,
    std::vector<ImportDiagnostic> source) -> void
  {
    destination.insert(destination.end(),
      std::make_move_iterator(source.begin()),
      std::make_move_iterator(source.end()));
  }

  auto AddError(ImportSourceJobAnalysis& report, std::string code,
    std::string message) -> void
  {
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = report.source_path.string(),
    });
  }

  struct AnalysisContext final {
    observer_ptr<detail::ImportSourceSnapshot> reader;
    observer_ptr<co::ThreadPool> pool;
    observer_ptr<ImportSourceJobAnalysis> report;
    std::stop_token stop;
    //! The identity of the file being analyzed, which its references are
    //! relative to.
    std::string anchor;
    std::unordered_map<std::string, size_t> files;

    auto AddFile(const std::filesystem::path& path, const bool required = true)
      -> void
    {
      if (path.empty()) {
        throw std::invalid_argument("Source path is empty");
      }
      const auto normalized
        = std::filesystem::absolute(path).lexically_normal();
      const auto [entry, inserted] = files.try_emplace(
        base::PathIdentityKey(normalized), report->files.size());
      if (inserted) {
        report->files.push_back({ .path = normalized, .required = required });
      } else {
        report->files.at(entry->second).required |= required;
      }
    }

    auto Read(const std::filesystem::path& path, const ReadOptions options = {})
      -> co::Co<serio::ByteBuffer>
    {
      if (stop.stop_requested()) {
        throw std::runtime_error("Source analysis canceled");
      }
      AddFile(path);
      auto bytes = co_await reader->ReadFile(path, options);
      if (!bytes) {
        if (const auto too_large = detail::SourceTooLargeDiagnostic(
              bytes.error(), path, reader->SourceLimit())) {
          throw std::runtime_error(too_large->message);
        }
        throw std::runtime_error(
          "Cannot read '" + path.string() + "': " + bytes.error().message());
      }
      co_return std::move(bytes).value();
    }

    auto ReadText(const std::filesystem::path& path) -> co::Co<std::string>
    {
      const auto buffer = co_await Read(path);
      const auto bytes = buffer.Bytes();
      auto text = std::string(bytes.size(), '\0');
      if (!bytes.empty()) {
        std::memcpy(text.data(), bytes.data(), bytes.size());
      }
      co_return text;
    }

    //! Reports a reference text the grammar refuses.
    auto Refuse(const ImportDependencyRefusal& refusal) const -> void
    {
      report->diagnostics.push_back(
        internal::RefuseReference(anchor, report->source_path.string(),
          refusal.written, refusal.error, refusal.json_pointer));
    }

    //! Resolves a reference text into `into`, or reports it refused.
    auto Resolve(std::vector<ImportLogicalDependency>& into,
      ImportDependencyText text, const VirtualPathRuleSet rules) const -> void
    {
      auto resolved = ResolveDependency(anchor, text, rules);
      if (!resolved.has_value()) {
        Refuse({
          .written = std::move(text.written),
          .json_pointer = std::move(text.json_pointer),
          .error = resolved.error(),
        });
        return;
      }
      into.push_back(std::move(*resolved));
    }

    auto Reference(std::string written, const Kind kind, std::string location,
      const VirtualPathRuleSet rules = VirtualPathRuleSet::kAuthored) const
      -> void
    {
      Resolve(report->references,
        {
          .written = std::move(written),
          .kind = kind,
          .json_pointer = std::move(location),
        },
        rules);
    }

    //! A product at a virtual path the job names.
    auto Output(const std::string& path, const Kind kind) const -> void
    {
      report->outputs.push_back({
        .written = path,
        .virtual_path = path,
        .kind = kind,
        .json_pointer = {},
        .required = true,
      });
    }
  };

  template <typename Adapter>
  auto PrepareModel(ImportRequest request, const serio::ByteBuffer& bytes,
    const std::stop_token& stop) -> ImportSourceJobAnalysis
  {
    auto report = ImportSourceJobAnalysis {};
    report.source_path = request.source_path;
    const auto source_id = request.source_path.string();
    if (auto placement = internal::PlaceModel(request); !placement) {
      report.diagnostics.push_back(std::move(placement.error()));
      return report;
    }
    auto naming = NamingService {};
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request = std::move(request);
    input.naming_service = observer_ptr { &naming };
    input.stop_token = stop;
    auto adapter = Adapter {};
    auto parsed = adapter.Parse(
      bytes.Bytes(), input, adapters::ModelParseMode::kMetadata);
    AppendDiagnostics(report.diagnostics, std::move(parsed.diagnostics));
    if (!parsed.success) {
      return report;
    }
    auto materials = adapter.PrepareMaterials(input);
    auto geometry = adapter.PrepareGeometry(input);
    auto textures = adapter.PrepareTextures(input);
    AppendDiagnostics(report.diagnostics, std::move(materials.diagnostics));
    AppendDiagnostics(report.diagnostics, std::move(geometry.diagnostics));
    AppendDiagnostics(report.diagnostics, std::move(textures.diagnostics));
    const auto product = [&](const std::string_view name,
                           const std::string_view extension) -> std::string {
      return internal::PlaceModelProduct(input.request, name, extension)
        .virtual_path;
    };
    const auto output = [&report](std::string path, const Kind kind,
                          const bool required) -> void {
      report.outputs.push_back({
        .written = path,
        .virtual_path = std::move(path),
        .kind = kind,
        .json_pointer = {},
        .required = required,
      });
    };
    // A remapped material is produced only when its target is missing.
    const auto& remap = input.request.options.material_remap;
    for (const auto& material : materials.sources) {
      output(product(material.material.storage_name,
               data::kMaterialDescriptorExtension),
        Kind::kMaterial, !remap.contains(material.material.storage_name));
    }
    for (const auto& source : geometry.sources) {
      output(product(source.name, data::kGeometryDescriptorExtension),
        Kind::kGeometry, true);
    }
    for (const auto& texture : textures.sources) {
      output(product(texture.name, data::kTextureDescriptorExtension),
        Kind::kTexture, false);
      if (!texture.embedded && !texture.source_path.empty()) {
        report.files.push_back(
          { .path = texture.source_path, .required = false });
        for (auto& fallback : adapters::TextureFallbackPaths(
               input.request.source_path, texture.source_path)) {
          report.files.push_back(
            { .path = std::move(fallback), .required = false });
        }
      }
    }
    if constexpr (std::is_same_v<Adapter, adapters::GltfAdapter>) {
      for (auto& path : adapter.CollectExternalBufferSources(input)) {
        report.files.push_back({ .path = std::move(path), .required = true });
      }
    }
    output(
      product(input.request.GetSceneName(), data::kSceneDescriptorExtension),
      Kind::kScene, true);
    auto facts = adapter.DescribeSource();
    auto names = internal::ModelProductNames {};
    for (const auto& material : materials.sources) {
      facts.materials.push_back(material.material.storage_name);
      names.materials.insert(material.material.storage_name);
    }
    for (const auto& texture : textures.sources) {
      facts.textures.push_back(texture.name);
      names.textures.insert(texture.name);
    }
    for (const auto& source : geometry.sources) {
      names.geometries.insert(source.name);
    }
    AppendDiagnostics(report.diagnostics,
      internal::ReportMissingSettingTargets(
        input.request.options, names, source_id));
    AppendDiagnostics(report.diagnostics, naming.CollisionWarnings(source_id));
    report.model = std::move(facts);
    report.complete = materials.success && geometry.success && textures.success;
    return report;
  }

  //! The bytes the metadata parse of a glTF source needs.
  struct GltfMetadata final {
    serio::ByteBuffer bytes;
    //! Whether the source is a binary glTF container.
    bool glb = false;
  };

  //! A `.glb`'s JSON chunk alone, found from its 12-byte header and first
  //! chunk header, so its binary chunk is never read; any other file whole.
  auto ReadGltfMetadata(AnalysisContext& context,
    const std::filesystem::path& path) -> co::Co<GltfMetadata>
  {
    constexpr auto kHeadersSize = uint64_t { 20 };
    constexpr auto kChunkLengthOffset = size_t { 12 };
    constexpr auto kChunkTypeOffset = size_t { 16 };
    constexpr auto kGlbMagic = uint32_t { 0x46546C67 }; // "glTF"
    constexpr auto kJsonChunk = uint32_t { 0x4E4F534A }; // "JSON"
    const auto headers = co_await context.Read(path,
      ReadOptions {
        .offset = 0U,
        .max_bytes = kHeadersSize,
      });
    // GLB words are little-endian, as on every platform the cooker runs on.
    const auto word = [&headers](const size_t offset) -> uint32_t {
      const auto value = serio::CopyAt<uint32_t>(
        headers.Bytes(), serio::ByteOffset { offset });
      return value ? *value : uint32_t { 0 };
    };
    if (headers.Size().get() < kHeadersSize || word(0) != kGlbMagic
      || word(kChunkTypeOffset) != kJsonChunk
      || word(kChunkLengthOffset) == 0U) {
      co_return GltfMetadata {
        .bytes = co_await context.Read(path),
        .glb = false,
      };
    }
    const auto json_size = uint64_t { word(kChunkLengthOffset) };
    co_return GltfMetadata {
      .bytes = co_await context.Read(path,
        ReadOptions {
          .offset = kHeadersSize,
          .max_bytes = json_size,
        }),
      .glb = true,
    };
  }

  //! The text a descriptor or sidecar job reads: its file, or its inline
  //! bindings when it names no file.
  auto ReadSourceText(AnalysisContext& context,
    const std::filesystem::path& source_path, const std::string& inline_json)
    -> co::Co<std::string>
  {
    if (source_path.empty()) {
      co_return inline_json;
    }
    context.report->source_path = source_path;
    co_return co_await context.ReadText(source_path);
  }

  //! The string field `field` of `parent`, when it has one.
  auto JsonString(const nlohmann::json& parent, const std::string_view field)
    -> std::optional<std::string>
  {
    if (!parent.is_object() || !parent.contains(field)
      || !parent.at(field).is_string()) {
      return std::nullopt;
    }
    return parent.at(field).get<std::string>();
  }

  //! The array `field` of `parent`, or an empty array.
  auto JsonItems(const nlohmann::json& parent, const std::string_view field)
    -> nlohmann::json
  {
    return parent.is_object() && parent.contains(field)
        && parent.at(field).is_array()
      ? parent.at(field)
      : nlohmann::json::array();
  }

  //! Reports each action an input source names by virtual path: another
  //! source's product, which the cook resolves, so that source cooks first.
  auto ReportInputActionReferences(
    const InputSourceDocument& document, const AnalysisContext& context) -> void
  {
    const auto reference
      = [&](const std::string& name, const std::string& json_pointer) -> void {
      if (IsInputActionReference(name)) {
        context.Reference(name, Kind::kInputAction, json_pointer);
      }
    };
    for (const auto& input_context : document.contexts) {
      for (const auto& mapping : input_context.mappings) {
        reference(mapping.action_name, mapping.json_pointer + "/action");
        for (const auto& trigger : mapping.triggers) {
          if (trigger.linked_action_name.has_value()) {
            reference(*trigger.linked_action_name,
              trigger.json_pointer + "/chord_action");
          }
          for (const auto& step : trigger.aux) {
            reference(step.action_name, step.json_pointer + "/action");
          }
        }
      }
    }
  }

  //! Analyzes an input source: its actions and contexts are products named
  //! under its identity, and the actions it names by virtual path are its
  //! references. The document is prepared as the cook prepares it.
  auto PrepareInputJob(const ImportManifestJob& job, AnalysisContext& context)
    -> co::Co<bool>
  {
    const auto text
      = co_await ReadSourceText(context, job.input.source_path, {});
    auto& report = *context.report;
    const auto document = PrepareInputDocument(
      text, report.source_path, job.virtual_path, report.diagnostics);
    if (!document.has_value()) {
      co_return false;
    }
    for (const auto& action : document->actions) {
      context.Output(action.virtual_path, Kind::kInputAction);
    }
    for (const auto& input_context : document->contexts) {
      context.Output(job.virtual_path + "/" + input_context.name
          + std::string(data::kInputMappingContextDescriptorExtension),
        Kind::kInputMappingContext);
    }
    ReportInputActionReferences(*document, context);
    co_return true;
  }

  //! Analyzes a script job: the script file is the only input, its
  //! declarations are checked as the cook checks them, and the script asset at
  //! the job's identity is its output.
  auto PrepareScriptJob(const ImportManifestJob& job, AnalysisContext& context)
    -> co::Co<bool>
  {
    auto& report = *context.report;
    report.source_path = job.script.source_path;
    const auto bytes = co_await context.Read(report.source_path);
    if (!AnalyzeScriptDeclarations(bytes.Bytes(), job.script.source_path,
          report.source_path, report.diagnostics)
          .has_value()) {
      co_return false;
    }
    context.Output(job.virtual_path, Kind::kScript);
    co_return true;
  }

  //! Analyzes a buffer container: it reads its buffers' source files and
  //! produces each buffer, named relative to the container's identity.
  auto PrepareBufferContainerJob(
    const ImportManifestJob& job, AnalysisContext& context) -> co::Co<bool>
  {
    auto& report = *context.report;
    const auto text = co_await ReadSourceText(
      context, job.buffer_container.descriptor_path, {});
    context.anchor = std::string {
      BufferContainerIdentity(job.virtual_path, job.source_mount_path),
    };
    CHECK_NOTNULL_F(job.mounts.get(), "A manifest job carries its mount table");
    const auto sources = internal::BufferSource::FromContainer(text,
      report.source_path, context.anchor, *job.mounts, report.diagnostics);
    if (!sources.has_value()) {
      co_return false;
    }
    for (const auto& source : *sources) {
      context.AddFile(source.source_path);
      context.Output(source.source_id, Kind::kBuffer);
    }
    co_return true;
  }

  //! Analyzes a physics material descriptor: the descriptor is the only
  //! input, and it references nothing.
  auto PreparePhysicsMaterialJob(
    const ImportManifestJob& job, AnalysisContext& context) -> co::Co<bool>
  {
    const auto text = co_await ReadSourceText(
      context, job.physics_material_descriptor.descriptor_path, {});
    auto& report = *context.report;
    if (!internal::PreparePhysicsMaterialDescriptor(
          text, report.source_path, report.diagnostics)
          .has_value()) {
      co_return false;
    }
    context.Output(job.virtual_path, Kind::kAsset);
    co_return true;
  }

  //! Analyzes a collision shape descriptor: it references its physics
  //! material.
  auto PrepareCollisionShapeJob(
    const ImportManifestJob& job, AnalysisContext& context) -> co::Co<bool>
  {
    const auto text = co_await ReadSourceText(
      context, job.collision_shape_descriptor.descriptor_path, {});
    auto& report = *context.report;
    const auto document = internal::PrepareCollisionShapeDescriptor(
      text, report.source_path, report.diagnostics);
    if (!document.has_value()) {
      co_return false;
    }
    context.Output(job.virtual_path, Kind::kAsset);
    if (const auto material = JsonString(*document, "material_ref")) {
      context.Reference(*material, Kind::kAsset, "/material_ref");
    }
    co_return true;
  }

  //! A sidecar's target scene: the anchor of its references, which the scene
  //! cook produces first.
  auto TargetScene(AnalysisContext& context, const std::string& scene) -> void
  {
    context.anchor = scene;
    context.report->references.push_back({
      .written = scene,
      .virtual_path = scene,
      .kind = Kind::kScene,
      .json_pointer = {},
      .required = true,
    });
  }

  //! Analyzes a physics sidecar: it references its target scene and the
  //! shapes, materials and meshes its bindings name, and produces the
  //! scene's physics companion beside it.
  auto PreparePhysicsSidecarJob(
    const ImportManifestJob& job, AnalysisContext& context) -> co::Co<bool>
  {
    const auto& settings = job.physics_sidecar;
    const auto text = co_await ReadSourceText(
      context, settings.source_path, settings.inline_bindings_json);
    if (!PreparePhysicsSidecarDocument(
          text, context.report->source_path, context.report->diagnostics)
          .has_value()) {
      co_return false;
    }
    // The prepared document is valid JSON.
    const auto document = std::optional { nlohmann::json::parse(text) };
    const auto& scene = settings.target_scene_virtual_path;
    TargetScene(context, scene);
    constexpr auto kSceneSuffix = data::kSceneDescriptorExtension;
    const auto stem = scene.ends_with(kSceneSuffix)
      ? scene.substr(0, scene.size() - kSceneSuffix.size())
      : scene;
    context.Output(stem + std::string(data::kPhysicsSceneDescriptorExtension),
      Kind::kPhysicsScene);
    for (const auto& reference : PhysicsSidecarReferences(*document)) {
      context.Reference(reference.written,
        reference.geometry ? Kind::kGeometry : Kind::kAsset,
        reference.json_pointer);
    }
    co_return true;
  }

  //! Analyzes a script sidecar: it references its target scene, which it
  //! rewrites, the scripts its bindings attach and the assets their
  //! parameters name.
  auto PrepareScriptSidecarJob(
    const ImportManifestJob& job, AnalysisContext& context) -> co::Co<bool>
  {
    const auto& settings = job.scripting_sidecar;
    const auto text = co_await ReadSourceText(
      context, settings.source_path, settings.inline_bindings_json);
    if (!PrepareScriptingSidecarDocument(
          text, context.report->source_path, context.report->diagnostics)
          .has_value()) {
      co_return false;
    }
    TargetScene(context, settings.target_scene_virtual_path);
    // The prepared document lists its rows, or is the array of rows itself.
    const auto document = std::optional { nlohmann::json::parse(text) };
    const auto rows
      = document->is_array() ? *document : document->at("bindings");
    const auto params
      = [&context](const nlohmann::json& row, const Pointer& location) -> void {
      const auto items = JsonItems(row, "params");
      for (size_t j = 0; j < items.size(); ++j) {
        if (JsonString(items[j], "type") != "asset") {
          continue;
        }
        if (const auto value = JsonString(items[j], "value")) {
          context.Reference(*value, Kind::kAsset,
            (location / "params" / j / "value").to_string());
        }
      }
    };
    // Rows given as an array are the bindings, as the cook reads them.
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto location = Pointer {} / "bindings" / i;
      if (const auto script = JsonString(rows[i], "script_virtual_path")) {
        context.Reference(*script, Kind::kScript,
          (location / "script_virtual_path").to_string());
      }
      params(rows[i], location);
    }
    const auto instances = JsonItems(*document, "instance_bindings");
    for (size_t i = 0; i < instances.size(); ++i) {
      params(instances[i], Pointer {} / "instance_bindings" / i);
    }
    co_return true;
  }

  auto PrepareJob(const ImportManifestJob& job, AnalysisContext& context)
    -> co::Co<bool>
  {
    if (job.job_type == "input") {
      co_return co_await PrepareInputJob(job, context);
    }
    if (job.job_type == "script") {
      co_return co_await PrepareScriptJob(job, context);
    }
    if (job.job_type == "buffer-container") {
      co_return co_await PrepareBufferContainerJob(job, context);
    }
    if (job.job_type == "physics-material-descriptor") {
      co_return co_await PreparePhysicsMaterialJob(job, context);
    }
    if (job.job_type == "collision-shape-descriptor") {
      co_return co_await PrepareCollisionShapeJob(job, context);
    }
    if (job.job_type == "physics-sidecar") {
      co_return co_await PreparePhysicsSidecarJob(job, context);
    }
    if (job.job_type == "script-sidecar") {
      co_return co_await PrepareScriptSidecarJob(job, context);
    }
    auto& report = *context.report;
    if (job.job_type == "material-descriptor") {
      report.source_path = job.material_descriptor.descriptor_path;
      const auto text = co_await context.ReadText(report.source_path);
      const auto source
        = MaterialSource::FromDescriptor(text, report.source_path,
          job.material_descriptor.job_name, report.diagnostics);
      if (!source) {
        co_return false;
      }
      context.Output(job.virtual_path, Kind::kMaterial);
      for (const auto& slot : MaterialSource::TextureSlots()) {
        const auto& binding = source->textures.*slot.binding;
        if (binding.assigned) {
          context.Reference(binding.source_id, Kind::kTexture,
            (Pointer {} / "textures" / std::string(slot.name)).to_string());
        }
      }
      co_return true;
    }
    if (job.job_type == "geometry-descriptor") {
      report.source_path = job.geometry_descriptor.descriptor_path;
      const auto text = co_await context.ReadText(report.source_path);
      CHECK_NOTNULL_F(
        job.mounts.get(), "A manifest job carries its mount table");
      const auto source = internal::GeometrySource::FromDescriptor(text,
        report.source_path, job.virtual_path, *job.mounts, report.diagnostics);
      if (!source) {
        co_return false;
      }
      context.Output(job.virtual_path, Kind::kGeometry);
      auto local = std::vector<std::string> {};
      for (const auto& buffer : source->buffers) {
        local.push_back(buffer.source_id);
        context.AddFile(buffer.source_path);
      }
      auto buffer_refs = std::vector<ImportDependencyText> {};
      const auto buffer_ref
        = [&](const std::string& path, const Pointer& location) -> void {
        buffer_refs.push_back({
          .written = path,
          .kind = Kind::kBuffer,
          .json_pointer = location.to_string(),
        });
      };
      for (size_t i = 0; i < source->lods.size(); ++i) {
        const auto& lod = source->lods.at(i);
        const auto location = Pointer {} / "lods" / i;
        const auto* skin
          = std::get_if<internal::GeometrySource::Skinned>(&lod.mesh);
        const auto* buffers = skin
          ? &skin->buffers
          : std::get_if<internal::GeometrySource::Buffers>(&lod.mesh);
        if (buffers) {
          buffer_ref(buffers->vertex, location / "buffers" / "vb_ref");
          buffer_ref(buffers->index, location / "buffers" / "ib_ref");
        }
        if (skin) {
          const auto skinning = location / "skinning";
          buffer_ref(skin->joint_index, skinning / "joint_index_ref");
          buffer_ref(skin->joint_weight, skinning / "joint_weight_ref");
          buffer_ref(skin->inverse_bind, skinning / "inverse_bind_ref");
          buffer_ref(skin->joint_remap, skinning / "joint_remap_ref");
          if (skin->skeleton) {
            context.Reference(*skin->skeleton, Kind::kAsset,
              (skinning / "skeleton_ref").to_string());
          }
        }
        for (size_t submesh = 0; submesh < lod.submeshes.size(); ++submesh) {
          context.Reference(lod.submeshes.at(submesh).material, Kind::kMaterial,
            (location / "submeshes" / submesh / "material_ref").to_string());
        }
      }
      // A local buffer's id is its resolved identity, a product of the job.
      for (const auto& buffer : local) {
        context.Output(buffer, Kind::kBuffer);
      }
      auto external
        = ExternalBufferReferences(job.virtual_path, local, buffer_refs);
      for (const auto& refusal : external.refused) {
        context.Refuse(refusal);
      }
      for (auto& dependency : external.dependencies) {
        report.references.push_back(std::move(dependency));
      }
      co_return true;
    }
    if (job.job_type == "scene-descriptor") {
      report.source_path = job.scene_descriptor.descriptor_path;
      const auto text = co_await context.ReadText(report.source_path);
      const auto source = internal::SceneSource::FromDescriptor(
        text, report.source_path, report.diagnostics);
      if (!source) {
        co_return false;
      }
      context.Output(job.virtual_path, Kind::kScene);
      for (size_t i = 0; i < source->renderables.size(); ++i) {
        const auto& renderable = source->renderables.at(i);
        context.Reference(renderable.geometry, Kind::kGeometry,
          (Pointer {} / "renderables" / i / "geometry_ref").to_string());
        for (const auto& entry : renderable.overrides) {
          if (const auto* asset
            = std::get_if<internal::SceneSource::AssetReference>(
              &entry.value)) {
            context.Reference(asset->virtual_path,
              AssetKind(entry.property->asset_type), entry.json_pointer);
          }
        }
      }
      // The scene cook composes each instanced scene, so each is cooked first.
      for (const auto& instance : source->instances) {
        context.Reference(instance.scene, Kind::kScene, instance.json_pointer);
        for (const auto& entry : instance.changes) {
          if (const auto* asset
            = std::get_if<internal::SceneSource::AssetReference>(
              &entry.change.value)) {
            context.Reference(asset->virtual_path,
              AssetKind(entry.change.property->asset_type),
              entry.change.json_pointer);
          }
        }
      }
      for (const auto& reference : source->references) {
        context.Reference(reference.virtual_path, AssetKind(reference.type),
          reference.json_pointer);
      }
      // The scene cook resolves each starting context, so each is cooked first.
      for (const auto& starting : source->starting_input_contexts) {
        context.Reference(
          starting.context, Kind::kInputMappingContext, starting.json_pointer);
      }
      if (source->fog && source->fog->cubemap) {
        context.Reference(*source->fog->cubemap, Kind::kTexture,
          "/environment/fog/inscattering_color_cubemap_ref");
      }
      if (source->sky_light && source->sky_light->cubemap) {
        context.Reference(*source->sky_light->cubemap, Kind::kTexture,
          "/environment/sky_light/cubemap_ref");
      }
      if (source->sky_sphere && source->sky_sphere->cubemap) {
        context.Reference(*source->sky_sphere->cubemap, Kind::kTexture,
          "/environment/sky_sphere/cubemap_ref");
      }
      if (source->post_process && source->post_process->metering_mask) {
        context.Reference(*source->post_process->metering_mask, Kind::kTexture,
          "/environment/post_process_volume/auto_exposure_metering_mask");
      }
      co_return true;
    }
    if (job.job_type == "texture" || job.job_type == "texture-descriptor") {
      auto settings = job.texture;
      report.source_path = settings.source_path;
      if (job.job_type == "texture-descriptor") {
        const auto text = co_await context.ReadText(report.source_path);
        const auto effective = TextureDescriptorImportSettings {
          .descriptor_path = settings.source_path,
          .texture = settings,
          .mounts = job.mounts,
        }.Prepare(text, report.diagnostics);
        if (!effective) {
          co_return false;
        }
        settings = *effective;
      }
      auto errors = std::ostringstream {};
      auto request = settings.Prepare(errors);
      if (!request) {
        throw std::invalid_argument(errors.str());
      }
      // Refuses an identity in no mount, as the cook would.
      static_cast<void>(request->GetTextureDescriptorRelPath());
      context.Output(request->asset_virtual_path, Kind::kTexture);
      const auto& tuning = request->options.texture_tuning;
      if (request->additional_sources.empty() && tuning.import_cubemap
        && !tuning.equirect_to_cubemap
        && tuning.cubemap_layout == CubeMapImageLayout::kUnknown) {
        const auto faces = co_await detail::DiscoverCubeFacePaths(
          request->source_path, *context.reader);
        if (!faces.has_value()) {
          throw std::runtime_error("Cannot find the cubemap faces of '"
            + request->source_path.string() + "': " + faces.error().message());
        }
        for (const auto& face : faces.value()) {
          context.AddFile(face);
        }
      } else if (request->additional_sources.empty()) {
        context.AddFile(request->source_path);
      } else {
        for (const auto& source : request->additional_sources) {
          context.AddFile(source.path);
        }
      }
      co_return true;
    }
    if (job.job_type == "gltf" || job.job_type == "fbx") {
      const auto& settings = job.job_type == "gltf" ? job.gltf : job.fbx;
      report.source_path = settings.source_path;
      auto errors = std::ostringstream {};
      auto request = settings.Prepare(
        job.job_type == "gltf" ? ImportFormat::kGltf : ImportFormat::kFbx,
        errors);
      if (!request) {
        throw std::invalid_argument(errors.str());
      }
      auto bytes = serio::ByteBuffer {};
      auto glb = false;
      if (request->GetFormat() == ImportFormat::kGltf) {
        auto metadata = co_await ReadGltfMetadata(context, report.source_path);
        bytes = std::move(metadata.bytes);
        glb = metadata.glb;
      } else {
        bytes = co_await context.Read(report.source_path);
      }
      auto facts = co_await context.pool->Run(
        [request = std::move(*request), bytes = std::move(bytes),
          stop = context.stop](
          co::ThreadPool::CancelToken) -> ImportSourceJobAnalysis {
          if (request.GetFormat() == ImportFormat::kGltf) {
            return PrepareModel<adapters::GltfAdapter>(request, bytes, stop);
          }
          return PrepareModel<adapters::FbxAdapter>(request, bytes, stop);
        });
      report.outputs = std::move(facts.outputs);
      report.model = std::move(facts.model);
      // The parse saw the JSON chunk of a container alone.
      if (glb && report.model.has_value()) {
        report.model->format = "glb";
      }
      AppendDiagnostics(report.diagnostics, std::move(facts.diagnostics));
      for (const auto& file : facts.files) {
        context.AddFile(file.path, file.required);
      }
      co_return facts.complete;
    }
    throw std::invalid_argument(
      "Source analysis does not support job type: " + job.job_type);
  }

  auto AnalyzeJob(const ImportManifestJob& job, IAsyncFileReader& reader,
    co::ThreadPool& pool, const std::stop_token stop,
    std::shared_ptr<const CapturedInputSet> captured_inputs)
    -> co::Co<ImportSourceJobAnalysis>
  {
    auto report = ImportSourceJobAnalysis {};
    report.source_path = job.SourcePath();
    report.id = job.id;
    report.job_type = job.job_type;
    auto snapshot
      = detail::ImportSourceSnapshot(reader, pool, std::move(captured_inputs));
    auto context = AnalysisContext {
      .reader = observer_ptr { &snapshot },
      .pool = observer_ptr { &pool },
      .report = observer_ptr { &report },
      .stop = stop,
      .anchor = job.virtual_path,
      .files = {},
    };
    auto prepared = false;
    try {
      prepared = co_await PrepareJob(job, context);
      for (const auto& file : report.files) {
        if (stop.stop_requested()) {
          throw std::runtime_error("Source analysis canceled");
        }
        const auto info = co_await snapshot.GetFileInfo(file.path);
        if (!info && info.error() != serio::Error::kNotFound) {
          throw std::runtime_error("Cannot stat '" + file.path.string()
            + "': " + info.error().message());
        }
        if (!info && file.required) {
          AddError(report, "analysis.source_missing",
            "Required source is missing: " + file.path.string());
        }
        if (info && file.required && info->is_directory) {
          AddError(report, "analysis.source_not_file",
            "Required source is a directory: " + file.path.string());
        }
      }
    } catch (const std::exception& error) {
      AddError(report,
        stop.stop_requested() ? "import.canceled" : "analysis.failed",
        error.what());
    }
    if (stop.stop_requested()) {
      report.accessed_paths = snapshot.AccessedPaths();
      co_return report;
    }
    try {
      co_await snapshot.Verify();
      report.observations = snapshot.Observations();
      report.complete = prepared && !stop.stop_requested()
        && std::ranges::none_of(
          report.diagnostics, [](const auto& item) -> auto {
            return item.severity == ImportSeverity::kError;
          });
    } catch (const std::exception& error) {
      AddError(report, "analysis.source_changed", error.what());
    }
    report.accessed_paths = snapshot.AccessedPaths();
    co_return report;
  }

  auto AnalyzeBatch(const ImportManifest& manifest, IAsyncFileReader& reader,
    co::ThreadPool& pool, ImportSourceAnalysis& result,
    const std::stop_token stop,
    std::shared_ptr<const CapturedInputSet> captured_inputs) -> co::Co<>
  {
    result.complete = true;
    result.jobs.reserve(manifest.jobs.size());
    for (const auto& job : manifest.jobs) {
      auto analyzed
        = co_await AnalyzeJob(job, reader, pool, stop, captured_inputs);
      result.complete = result.complete && analyzed.complete;
      result.jobs.push_back(std::move(analyzed));
    }
  }

  auto ModelJson(const ModelSourceFacts& facts) -> nlohmann::json
  {
    using nlohmann::json;
    auto scenes = json::array();
    for (const auto& scene : facts.scenes) {
      scenes.push_back({
        { "index", scene.index },
        { "name", scene.name },
        { "selector", scene.selector },
        { "is_default", scene.is_default },
        { "root_count", scene.root_count },
        { "node_count", scene.node_count },
      });
    }
    auto unit_meters = json(nullptr);
    if (facts.unit_meters.has_value()) {
      unit_meters = *facts.unit_meters;
    }
    auto left_handed = json(nullptr);
    if (facts.left_handed.has_value()) {
      left_handed = *facts.left_handed;
    }
    auto format_version = json(nullptr);
    if (facts.format_version.has_value()) {
      format_version = *facts.format_version;
    }
    return {
      { "format", facts.format },
      { "format_version", std::move(format_version) },
      { "unit_meters", std::move(unit_meters) },
      { "right", facts.right },
      { "up", facts.up },
      { "front", facts.front },
      { "left_handed", std::move(left_handed) },
      { "node_count", facts.node_count },
      { "mesh_count", facts.mesh_count },
      { "scenes", std::move(scenes) },
      { "materials", facts.materials },
      { "textures", facts.textures },
    };
  }

  auto DigestText(const base::Sha256Digest& digest) -> std::string
  {
    auto text = std::string {};
    text.reserve(digest.size() * 2U);
    for (const auto byte : digest) {
      fmt::format_to(std::back_inserter(text), "{:02x}", byte);
    }
    return text;
  }
} // namespace

namespace detail {
  auto RunSourceAnalysis(const ImportManifest& manifest, ImportEventLoop& loop,
    IAsyncFileReader& reader, co::ThreadPool& pool,
    const std::stop_token& stop_token,
    std::shared_ptr<const CapturedInputSet> captured_inputs)
    -> ImportSourceAnalysis
  {
    auto report = ImportSourceAnalysis {};
    report.producer_version = oxygen::version::Version();
    auto canceled = std::make_shared<co::Event>();
    const auto cancel_callback
      = std::stop_callback(stop_token, [&loop, canceled] -> void {
          loop.Post([canceled] -> void { canceled->Trigger(); });
        });
    static_cast<void>(co::Run(loop,
      co::AnyOf(AnalyzeBatch(manifest, reader, pool, report, stop_token,
                  std::move(captured_inputs)),
        *canceled)));
    report.complete = report.complete && !stop_token.stop_requested();
    return report;
  }
} // namespace detail

auto ImportManifest::AnalyzeSources(const std::stop_token& stop_token,
  std::shared_ptr<const CapturedInputSet> captured_inputs) const
  -> ImportSourceAnalysis
{
  auto loop = ImportEventLoop {};
  auto reader = CreateAsyncFileReader(
    loop.Driver(), max_source_bytes.value_or(kDefaultMaxSourceBytes));
  auto pool = co::ThreadPool(loop, 1U);
  return detail::RunSourceAnalysis(
    *this, loop, *reader, pool, stop_token, std::move(captured_inputs));
}

auto ResolveDependency(const std::string_view anchor,
  const ImportDependencyText& text, const VirtualPathRuleSet rules)
  -> std::expected<ImportLogicalDependency, VirtualPathError>
{
  auto resolved
    = oxygen::content::ResolveReference(anchor, text.written, rules);
  if (!resolved.has_value()) {
    return std::unexpected(resolved.error());
  }
  return ImportLogicalDependency {
    .written = text.written,
    .virtual_path = std::move(*resolved),
    .kind = text.kind,
    .json_pointer = text.json_pointer,
    .required = true,
  };
}

auto IsInputActionReference(const std::string_view text) -> bool
{
  return text.ends_with(data::kInputActionDescriptorExtension);
}

auto ExternalBufferReferences(const std::string_view anchor,
  const std::span<const std::string> local_buffers,
  const std::span<const ImportDependencyText> references)
  -> ImportDependencyResolution
{
  // Local buffers are resolved identities already.
  const auto local = std::unordered_set<std::string>(
    local_buffers.begin(), local_buffers.end());
  auto result = ImportDependencyResolution {};
  for (const auto& reference : references) {
    auto resolved
      = ResolveDependency(anchor, reference, VirtualPathRuleSet::kCooked);
    if (!resolved.has_value()) {
      result.refused.push_back({
        .written = reference.written,
        .json_pointer = reference.json_pointer,
        .error = resolved.error(),
      });
    } else if (!local.contains(resolved->virtual_path)) {
      result.dependencies.push_back(std::move(*resolved));
    }
  }
  return result;
}

auto ImportSourceAnalysis::ToJson() const -> std::string
{
  using nlohmann::json;
  auto serialized_jobs = json::array();
  const auto serialize_diagnostics
    = [](const std::vector<ImportDiagnostic>& diagnostics) -> json {
    auto array = json::array();
    for (const auto& diagnostic : diagnostics) {
      array.push_back({
        { "severity", to_string(diagnostic.severity) },
        { "code", diagnostic.code },
        { "message", diagnostic.message },
        { "source_path", diagnostic.source_path },
        { "object_path", diagnostic.object_path },
        { "json_pointer", diagnostic.json_pointer },
        { "object_name", diagnostic.object_name },
      });
    }
    return array;
  };
  const auto logical
    = [](const std::vector<ImportLogicalDependency>& dependencies)
    -> nlohmann::json {
    auto array = json::array();
    for (const auto& dependency : dependencies) {
      array.push_back({
        { "written", dependency.written },
        { "virtual_path", dependency.virtual_path },
        { "kind", KindName(dependency.kind) },
        { "json_pointer", dependency.json_pointer },
        { "required", dependency.required },
      });
    }
    return array;
  };
  for (const auto& job : jobs) {
    auto files = json::array();
    for (const auto& file : job.files) {
      files.push_back(
        { { "path", PathText(file.path) }, { "required", file.required } });
    }
    auto accessed = json::array();
    for (const auto& path : job.accessed_paths) {
      accessed.push_back(PathText(path));
    }
    auto observations = json::array();
    for (const auto& observation : job.observations) {
      auto reads = json::array();
      for (const auto& read : observation.reads) {
        reads.push_back({
          { "offset", read.offset },
          { "max_bytes", read.max_bytes },
          { "sha256", DigestText(read.digest) },
        });
      }
      auto metadata
        = observation.metadata
            .transform([](const FileInfo& info) -> json {
              const auto timestamp
                = std::chrono::clock_cast<std::chrono::system_clock>(
                  info.last_modified);
              const auto seconds
                = std::chrono::floor<std::chrono::seconds>(timestamp);
              const auto fraction
                = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  timestamp - seconds);
              return json {
                { "size", info.size },
                { "is_directory", info.is_directory },
                { "is_symlink", info.is_symlink },
                { "last_modified_seconds", seconds.time_since_epoch().count() },
                { "last_modified_nanoseconds", fraction.count() },
              };
            })
            .value_or(json(nullptr));
      observations.push_back({
        { "path", PathText(observation.path) },
        { "exists", observation.exists },
        { "metadata", std::move(metadata) },
        { "reads", std::move(reads) },
      });
    }
    auto serialized = json {
      { "id", job.id },
      { "job_type", job.job_type },
      { "source_path", PathText(job.source_path) },
      { "complete", job.complete },
      { "outputs", logical(job.outputs) },
      { "references", logical(job.references) },
      { "files", std::move(files) },
      { "observations", std::move(observations) },
      { "accessed_paths", std::move(accessed) },
      { "diagnostics", serialize_diagnostics(job.diagnostics) },
    };
    if (const auto& model = job.model; model.has_value()) {
      serialized["model"] = ModelJson(*model);
    }
    serialized_jobs.push_back(std::move(serialized));
  }
  return json {
    { "version", version },
    { "producer_version", producer_version },
    { "complete", complete },
    { "jobs", std::move(serialized_jobs) },
    { "diagnostics", serialize_diagnostics(diagnostics) },
  }
    .dump(2);
}

} // namespace oxygen::content::import
