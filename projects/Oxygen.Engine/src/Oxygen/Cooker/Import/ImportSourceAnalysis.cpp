//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
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
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceAnalysis.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/GeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/SourceAnalysisRunner.h>
#include <Oxygen/Cooker/Import/Internal/TextureSourceAssembly_internal.h>
#include <Oxygen/Cooker/Import/Internal/fbx/FbxAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Version.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content::import {
namespace {
  using Kind = ImportDependencyKind;

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
      .object_path = {},
    });
  }

  struct AnalysisContext final {
    detail::ImportSourceSnapshot& reader;
    co::ThreadPool& pool;
    ImportSourceJobAnalysis& report;
    std::stop_token stop;
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
        base::PathIdentityKey(normalized), report.files.size());
      if (inserted) {
        report.files.push_back({ .path = normalized, .required = required });
      } else {
        report.files.at(entry->second).required |= required;
      }
    }

    auto Read(const std::filesystem::path& path)
      -> co::Co<std::vector<std::byte>>
    {
      if (stop.stop_requested()) {
        throw std::runtime_error("Source analysis canceled");
      }
      AddFile(path);
      auto bytes = co_await reader.ReadFile(path);
      if (!bytes) {
        throw std::runtime_error(bytes.error().ToString());
      }
      co_return std::move(bytes).value();
    }

    auto ReadText(const std::filesystem::path& path) -> co::Co<std::string>
    {
      const auto bytes = co_await Read(path);
      auto text = std::string(bytes.size(), '\0');
      if (!bytes.empty()) {
        std::memcpy(text.data(), bytes.data(), bytes.size());
      }
      co_return text;
    }

    auto Reference(std::string path, const Kind kind, std::string location)
      -> void
    {
      report.references.push_back({ .virtual_path = std::move(path),
        .kind = kind,
        .object_path = std::move(location),
        .required = true });
    }

    auto Output(std::string path, const Kind kind) -> void
    {
      report.outputs.push_back({ .virtual_path = std::move(path),
        .kind = kind,
        .object_path = {},
        .required = true });
    }
  };

  template <typename Adapter>
  auto PrepareModel(ImportRequest request, const std::vector<std::byte>& bytes,
    const std::stop_token stop) -> ImportSourceJobAnalysis
  {
    auto report = ImportSourceJobAnalysis {};
    report.source_path = request.source_path;
    const auto source_id = request.source_path.string();
    const auto layout = request.loose_cooked_layout;
    auto naming = NamingService(request.options.naming_strategy);
    auto input = adapters::AdapterInput {};
    input.source_id_prefix = source_id;
    input.request = std::move(request);
    input.naming_service = observer_ptr { &naming };
    input.stop_token = stop;
    auto adapter = Adapter {};
    auto parsed = adapter.Parse(std::span<const std::byte>(bytes), input,
      adapters::ModelParseMode::kMetadata);
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
    for (const auto& material : materials.sources) {
      report.outputs.push_back({ .virtual_path
        = layout.MaterialVirtualPath(material.material.storage_name),
        .kind = Kind::kMaterial,
        .object_path = {},
        .required = true });
    }
    for (const auto& source : geometry.sources) {
      report.outputs.push_back(
        { .virtual_path = layout.GeometryVirtualPath(source.name),
          .kind = Kind::kGeometry,
          .object_path = {},
          .required = true });
    }
    for (const auto& texture : textures.sources) {
      report.outputs.push_back({ .virtual_path
        = layout.DescriptorVirtualPath(layout.TextureDescriptorRelPath(
          texture.source_id, texture.source_id)),
        .kind = Kind::kTexture,
        .object_path = {},
        .required = false });
      if (!texture.embedded && !texture.source_path.empty()) {
        report.files.push_back(
          { .path = texture.source_path, .required = false });
      }
    }
    if constexpr (std::is_same_v<Adapter, adapters::GltfAdapter>) {
      for (auto& path : adapter.CollectExternalBufferSources(input)) {
        report.files.push_back({ .path = std::move(path), .required = true });
      }
    }
    report.outputs.push_back(
      { .virtual_path = layout.SceneVirtualPath(input.request.GetSceneName()),
        .kind = Kind::kScene,
        .object_path = {},
        .required = true });
    report.complete = materials.success && geometry.success && textures.success;
    return report;
  }

  auto PrepareJob(const ImportManifestJob& job, AnalysisContext& context)
    -> co::Co<bool>
  {
    auto& report = context.report;
    const auto& layout = job.loose_cooked_layout;
    if (job.job_type == "material-descriptor") {
      report.source_path = job.material_descriptor.descriptor_path;
      const auto text = co_await context.ReadText(report.source_path);
      const auto source
        = MaterialSource::FromDescriptor(text, report.source_path,
          job.material_descriptor.job_name, report.diagnostics);
      if (!source) {
        co_return false;
      }
      context.Output(
        layout.MaterialVirtualPath(source->storage_name), Kind::kMaterial);
      for (const auto& slot : MaterialSource::TextureSlots()) {
        const auto& binding = source->textures.*slot.binding;
        if (binding.assigned) {
          context.Reference(binding.source_id, Kind::kTexture,
            "textures." + std::string(slot.name));
        }
      }
      co_return true;
    }
    if (job.job_type == "geometry-descriptor") {
      report.source_path = job.geometry_descriptor.descriptor_path;
      const auto text = co_await context.ReadText(report.source_path);
      const auto source = internal::GeometrySource::FromDescriptor(
        text, report.source_path, report.diagnostics);
      if (!source) {
        co_return false;
      }
      context.Output(layout.GeometryVirtualPath(source->name), Kind::kGeometry);
      auto local = std::unordered_set<std::string> {};
      for (const auto& buffer : source->buffers) {
        local.insert(buffer.source_id);
        context.Output(buffer.source_id, Kind::kBuffer);
        context.AddFile(buffer.source_path);
      }
      const auto buffer_ref
        = [&](const std::string& path, const std::string& location) {
            if (!local.contains(path)) {
              context.Reference(path, Kind::kBuffer, location);
            }
          };
      for (size_t i = 0; i < source->lods.size(); ++i) {
        const auto& lod = source->lods.at(i);
        const auto location = "lods[" + std::to_string(i) + "]";
        const auto* skin
          = std::get_if<internal::GeometrySource::Skinned>(&lod.mesh);
        const auto* buffers = skin
          ? &skin->buffers
          : std::get_if<internal::GeometrySource::Buffers>(&lod.mesh);
        if (buffers) {
          buffer_ref(buffers->vertex, location + ".buffers.vb_ref");
          buffer_ref(buffers->index, location + ".buffers.ib_ref");
        }
        if (skin) {
          buffer_ref(skin->joint_index, location + ".skinning.joint_index_ref");
          buffer_ref(
            skin->joint_weight, location + ".skinning.joint_weight_ref");
          buffer_ref(
            skin->inverse_bind, location + ".skinning.inverse_bind_ref");
          buffer_ref(skin->joint_remap, location + ".skinning.joint_remap_ref");
          if (skin->skeleton) {
            context.Reference(*skin->skeleton, Kind::kAsset,
              location + ".skinning.skeleton_ref");
          }
        }
        for (size_t submesh = 0; submesh < lod.submeshes.size(); ++submesh) {
          context.Reference(lod.submeshes.at(submesh).material, Kind::kMaterial,
            location + ".submeshes[" + std::to_string(submesh)
              + "].material_ref");
        }
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
      // The scene pipeline derives its name from this authored name as a path.
      auto scene_request = ImportRequest {};
      scene_request.source_path = source->name;
      context.Output(
        layout.SceneVirtualPath(scene_request.GetSceneName()), Kind::kScene);
      for (size_t i = 0; i < source->renderables.size(); ++i) {
        const auto& renderable = source->renderables.at(i);
        const auto location = "renderables[" + std::to_string(i) + "]";
        context.Reference(
          renderable.geometry, Kind::kGeometry, location + ".geometry_ref");
        for (const auto& material : renderable.materials) {
          context.Reference(material.material, Kind::kMaterial,
            location + ".material_overrides");
        }
      }
      for (const auto& reference : source->references) {
        context.Reference(reference.virtual_path, AssetKind(reference.type),
          reference.object_path);
      }
      if (source->fog && source->fog->cubemap) {
        context.Reference(*source->fog->cubemap, Kind::kTexture,
          "environment.fog.inscattering_color_cubemap_ref");
      }
      if (source->sky_light && source->sky_light->cubemap) {
        context.Reference(*source->sky_light->cubemap, Kind::kTexture,
          "environment.sky_light.cubemap_ref");
      }
      if (source->post_process && source->post_process->metering_mask) {
        context.Reference(*source->post_process->metering_mask, Kind::kTexture,
          "environment.post_process_volume.auto_exposure_metering_mask");
      }
      co_return true;
    }
    if (job.job_type == "texture" || job.job_type == "texture-descriptor") {
      auto settings = job.texture;
      report.source_path = settings.source_path;
      if (job.job_type == "texture-descriptor") {
        const auto text = co_await context.ReadText(report.source_path);
        const auto effective = TextureDescriptorImportSettings {
          .descriptor_path = settings.source_path, .texture = settings,
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
      request->loose_cooked_layout = layout;
      context.Output(
        layout.DescriptorVirtualPath(request->GetTextureDescriptorRelPath()),
        Kind::kTexture);
      const auto& tuning = request->options.texture_tuning;
      if (request->additional_sources.empty() && tuning.import_cubemap
        && !tuning.equirect_to_cubemap
        && tuning.cubemap_layout == CubeMapImageLayout::kUnknown) {
        const auto faces = co_await detail::DiscoverCubeFacePaths(
          request->source_path, context.reader);
        if (!faces.has_value()) {
          throw std::runtime_error(faces.error().ToString());
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
      auto provenance = std::string {};
      if (!settings.material_slot_provenance_path.empty()) {
        provenance
          = co_await context.ReadText(settings.material_slot_provenance_path);
      }
      auto errors = std::ostringstream {};
      auto request = settings.Prepare(
        job.job_type == "gltf" ? ImportFormat::kGltf : ImportFormat::kFbx,
        provenance, errors);
      if (!request) {
        throw std::invalid_argument(errors.str());
      }
      request->loose_cooked_layout = layout;
      auto bytes = co_await context.Read(report.source_path);
      auto facts = co_await context.pool.Run(
        [request = std::move(*request), bytes = std::move(bytes),
          stop = context.stop](
          co::ThreadPool::CancelToken) -> ImportSourceJobAnalysis {
          if (request.GetFormat() == ImportFormat::kGltf) {
            return PrepareModel<adapters::GltfAdapter>(request, bytes, stop);
          }
          return PrepareModel<adapters::FbxAdapter>(request, bytes, stop);
        });
      report.outputs = std::move(facts.outputs);
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
    auto context = AnalysisContext { .reader = snapshot,
      .pool = pool,
      .report = report,
      .stop = stop,
      .files = {} };
    auto prepared = false;
    try {
      prepared = co_await PrepareJob(job, context);
      for (const auto& file : report.files) {
        if (stop.stop_requested()) {
          throw std::runtime_error("Source analysis canceled");
        }
        const auto info = co_await snapshot.GetFileInfo(file.path);
        if (!info && info.error().code != FileError::kNotFound) {
          throw std::runtime_error(info.error().ToString());
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
        && std::ranges::none_of(report.diagnostics, [](const auto& item) {
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
    const std::stop_token stop_token,
    std::shared_ptr<const CapturedInputSet> captured_inputs)
    -> ImportSourceAnalysis
  {
    auto report = ImportSourceAnalysis {};
    report.producer_version = oxygen::version::Version();
    auto canceled = std::make_shared<co::Event>();
    const auto cancel_callback = std::stop_callback(stop_token,
      [&loop, canceled] { loop.Post([canceled] { canceled->Trigger(); }); });
    static_cast<void>(co::Run(loop,
      co::AnyOf(AnalyzeBatch(manifest, reader, pool, report, stop_token,
                  std::move(captured_inputs)),
        *canceled)));
    report.complete = report.complete && !stop_token.stop_requested();
    return report;
  }
} // namespace detail

auto ImportManifest::AnalyzeSources(const std::stop_token stop_token,
  std::shared_ptr<const CapturedInputSet> captured_inputs) const
  -> ImportSourceAnalysis
{
  auto loop = ImportEventLoop {};
  auto reader = CreateAsyncFileReader(loop);
  auto pool = co::ThreadPool(loop, 1U);
  return detail::RunSourceAnalysis(
    *this, loop, *reader, pool, stop_token, std::move(captured_inputs));
}

auto ImportSourceAnalysis::ToJson() const -> std::string
{
  using nlohmann::json;
  auto serialized_jobs = json::array();
  const auto logical
    = [](const std::vector<ImportLogicalDependency>& dependencies) {
        auto array = json::array();
        for (const auto& dependency : dependencies) {
          array.push_back({ { "virtual_path", dependency.virtual_path },
            { "kind", KindName(dependency.kind) },
            { "object_path", dependency.object_path },
            { "required", dependency.required } });
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
        reads.push_back(
          { { "offset", read.offset }, { "max_bytes", read.max_bytes },
            { "sha256", DigestText(read.digest) } });
      }
      auto metadata
        = observation.metadata
            .transform([](const FileInfo& info) {
              const auto timestamp
                = std::chrono::clock_cast<std::chrono::system_clock>(
                  info.last_modified);
              const auto seconds
                = std::chrono::floor<std::chrono::seconds>(timestamp);
              const auto fraction
                = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  timestamp - seconds);
              return json { { "size", info.size },
                { "is_directory", info.is_directory },
                { "is_symlink", info.is_symlink },
                { "last_modified_seconds", seconds.time_since_epoch().count() },
                { "last_modified_nanoseconds", fraction.count() } };
            })
            .value_or(json(nullptr));
      observations.push_back({ { "path", PathText(observation.path) },
        { "exists", observation.exists }, { "metadata", std::move(metadata) },
        { "reads", std::move(reads) } });
    }
    auto diagnostics = json::array();
    for (const auto& diagnostic : job.diagnostics) {
      diagnostics.push_back({ { "severity", to_string(diagnostic.severity) },
        { "code", diagnostic.code }, { "message", diagnostic.message },
        { "source_path", diagnostic.source_path },
        { "object_path", diagnostic.object_path } });
    }
    serialized_jobs.push_back({ { "id", job.id }, { "job_type", job.job_type },
      { "source_path", PathText(job.source_path) },
      { "complete", job.complete }, { "outputs", logical(job.outputs) },
      { "references", logical(job.references) }, { "files", std::move(files) },
      { "observations", std::move(observations) },
      { "accessed_paths", std::move(accessed) },
      { "diagnostics", std::move(diagnostics) } });
  }
  return json {
    { "version", version }, { "producer_version", producer_version },
    { "complete", complete }, { "jobs", std::move(serialized_jobs) }
  }.dump(2);
}

} // namespace oxygen::content::import
