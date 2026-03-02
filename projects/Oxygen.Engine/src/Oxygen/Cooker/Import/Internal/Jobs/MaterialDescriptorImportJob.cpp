//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/MaterialDescriptorImportJob.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json_schema::json_validator;

  struct TextureSlotBindingEntry final {
    std::string_view slot_name;
    MaterialTextureBinding MaterialTextureBindings::* binding_member;
  };

  constexpr std::array kTextureSlotBindings {
    TextureSlotBindingEntry {
      "base_color", &MaterialTextureBindings::base_color },
    TextureSlotBindingEntry { "normal", &MaterialTextureBindings::normal },
    TextureSlotBindingEntry { "metallic", &MaterialTextureBindings::metallic },
    TextureSlotBindingEntry {
      "roughness", &MaterialTextureBindings::roughness },
    TextureSlotBindingEntry {
      "ambient_occlusion", &MaterialTextureBindings::ambient_occlusion },
    TextureSlotBindingEntry { "emissive", &MaterialTextureBindings::emissive },
    TextureSlotBindingEntry { "specular", &MaterialTextureBindings::specular },
    TextureSlotBindingEntry {
      "sheen_color", &MaterialTextureBindings::sheen_color },
    TextureSlotBindingEntry {
      "clearcoat", &MaterialTextureBindings::clearcoat },
    TextureSlotBindingEntry {
      "clearcoat_normal", &MaterialTextureBindings::clearcoat_normal },
    TextureSlotBindingEntry {
      "transmission", &MaterialTextureBindings::transmission },
    TextureSlotBindingEntry {
      "thickness", &MaterialTextureBindings::thickness },
  };

  constexpr uint16_t kSidecarDescriptorVersion = 1;

#pragma pack(push, 1)
  struct TextureSidecarFile final {
    char magic[4] = { 'O', 'T', 'E', 'X' };
    uint16_t version = kSidecarDescriptorVersion;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::render::TextureResourceDesc descriptor {};
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<TextureSidecarFile>);

  auto GetMaterialDescriptorValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      out.set_root_schema(nlohmann::json::parse(kMaterialDescriptorSchema));
      return out;
    }();
    return validator;
  }

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

  auto ValidateDescriptorSchema(ImportSession& session,
    const ImportRequest& request, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "material.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Material descriptor validation failed: ",
      .validation_overflow_prefix = "Material descriptor validation emitted ",
      .validator_failure_code = "material.descriptor.schema_validator_failure",
      .validator_failure_prefix
      = "Material descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetMaterialDescriptorValidator(), descriptor_doc, config,
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

  auto ParseTextureSidecar(std::span<const std::byte> bytes,
    TextureSidecarFile& file, std::string& error_message) -> bool
  {
    if (bytes.size() < sizeof(TextureSidecarFile)) {
      error_message = "Texture descriptor file is truncated";
      return false;
    }

    std::memcpy(&file, bytes.data(), sizeof(TextureSidecarFile));
    if (std::memcmp(file.magic, "OTEX", 4) != 0) {
      error_message = "Texture descriptor has invalid magic";
      return false;
    }
    if (file.version != kSidecarDescriptorVersion) {
      error_message = "Texture descriptor has unsupported version";
      return false;
    }
    return true;
  }

  auto ToLowerAscii(std::string value) -> std::string
  {
    for (auto& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
  }

  auto IsHexString(const std::string_view text) -> bool
  {
    if (text.empty()) {
      return false;
    }
    for (const auto ch : text) {
      if (!std::isxdigit(static_cast<unsigned char>(ch))) {
        return false;
      }
    }
    return true;
  }

  auto ResolveHashedTextureDescriptorPath(
    const std::filesystem::path& plain_path,
    std::filesystem::path& resolved_path, std::string& error_message) -> bool
  {
    constexpr size_t kHashHexLength = 16;

    const auto parent = plain_path.parent_path();
    if (!std::filesystem::exists(parent)
      || !std::filesystem::is_directory(parent)) {
      return false;
    }

    const auto ext = ToLowerAscii(plain_path.extension().string());
    if (ext != ".otex") {
      return false;
    }

    const auto plain_stem = plain_path.stem().string();
    if (plain_stem.empty()) {
      return false;
    }

    std::vector<std::filesystem::path> matches;
    const auto expected_prefix = ToLowerAscii(plain_stem) + "_";

    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto candidate = entry.path();
      if (ToLowerAscii(candidate.extension().string()) != ".otex") {
        continue;
      }

      const auto candidate_stem = candidate.stem().string();
      const auto candidate_lower = ToLowerAscii(candidate_stem);
      if (!candidate_lower.starts_with(expected_prefix)) {
        continue;
      }
      const auto suffix = candidate_lower.substr(expected_prefix.size());
      if (suffix.size() != kHashHexLength || !IsHexString(suffix)) {
        continue;
      }
      matches.push_back(candidate);
    }

    if (matches.empty()) {
      return false;
    }
    if (matches.size() > 1U) {
      error_message = "Multiple hashed texture descriptors match virtual path";
      return false;
    }

    resolved_path = std::move(matches.front());
    return true;
  }

  auto ResolveTextureIndexFromVirtualPath(ImportSession& session,
    const ImportRequest& request, IAsyncFileReader& reader,
    std::string_view virtual_path, std::string object_path)
    -> co::Co<std::optional<uint32_t>>
  {
    if (!IsCanonicalVirtualPath(virtual_path)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "material.descriptor.texture_virtual_path_invalid",
        "Texture reference virtual_path must be canonical",
        std::move(object_path));
      co_return std::nullopt;
    }

    auto relpath = std::string {};
    if (!TryVirtualPathToRelPath(request, virtual_path, relpath)) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "material.descriptor.texture_virtual_path_unmounted",
        "Texture reference virtual_path is outside mounted cooked roots",
        std::move(object_path));
      co_return std::nullopt;
    }

    auto mounted_roots = std::vector<std::filesystem::path> {};
    if (request.cooked_root.has_value()) {
      mounted_roots.push_back(*request.cooked_root);
    } else {
      mounted_roots.push_back(request.source_path.parent_path());
    }
    for (const auto& root : request.cooked_context_roots) {
      mounted_roots.push_back(root);
    }

    for (auto it = mounted_roots.rbegin(); it != mounted_roots.rend(); ++it) {
      auto descriptor_path = *it / std::filesystem::path(relpath);
      if (!std::filesystem::exists(descriptor_path)) {
        auto alias_resolve_error = std::string {};
        auto resolved_hashed = std::filesystem::path {};
        if (!ResolveHashedTextureDescriptorPath(
              descriptor_path, resolved_hashed, alias_resolve_error)) {
          if (!alias_resolve_error.empty()) {
            AddDiagnostic(session, request, ImportSeverity::kError,
              "material.descriptor.texture_descriptor_ambiguous",
              alias_resolve_error + ": " + descriptor_path.string(),
              object_path);
            co_return std::nullopt;
          }
          continue;
        }
        descriptor_path = std::move(resolved_hashed);
      }

      const auto read_result = co_await reader.ReadFile(descriptor_path);
      if (!read_result.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "material.descriptor.texture_descriptor_read_failed",
          "Failed reading texture descriptor: "
            + read_result.error().ToString(),
          object_path);
        co_return std::nullopt;
      }

      auto sidecar = TextureSidecarFile {};
      auto parse_error = std::string {};
      if (!ParseTextureSidecar(read_result.value(), sidecar, parse_error)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "material.descriptor.texture_descriptor_invalid", parse_error,
          object_path);
        co_return std::nullopt;
      }

      co_return sidecar.resource_index.get();
    }

    AddDiagnostic(session, request, ImportSeverity::kError,
      "material.descriptor.texture_descriptor_missing",
      "Texture descriptor virtual_path was not found: "
        + std::string(virtual_path),
      std::move(object_path));
    co_return std::nullopt;
  }

  auto ParseMaterialDomain(const std::string_view domain)
    -> std::optional<data::MaterialDomain>
  {
    using data::MaterialDomain;
    if (domain == "opaque") {
      return MaterialDomain::kOpaque;
    }
    if (domain == "alpha_blended") {
      return MaterialDomain::kAlphaBlended;
    }
    if (domain == "masked") {
      return MaterialDomain::kMasked;
    }
    if (domain == "decal") {
      return MaterialDomain::kDecal;
    }
    if (domain == "ui") {
      return MaterialDomain::kUserInterface;
    }
    if (domain == "post_process") {
      return MaterialDomain::kPostProcess;
    }
    return std::nullopt;
  }

  auto ParseAlphaMode(const std::string_view alpha_mode)
    -> std::optional<MaterialAlphaMode>
  {
    if (alpha_mode == "opaque") {
      return MaterialAlphaMode::kOpaque;
    }
    if (alpha_mode == "masked") {
      return MaterialAlphaMode::kMasked;
    }
    if (alpha_mode == "blended") {
      return MaterialAlphaMode::kBlended;
    }
    return std::nullopt;
  }

  auto ParseOrmPolicy(const std::string_view policy) -> std::optional<OrmPolicy>
  {
    if (policy == "auto") {
      return OrmPolicy::kAuto;
    }
    if (policy == "force_packed") {
      return OrmPolicy::kForcePacked;
    }
    if (policy == "force_separate") {
      return OrmPolicy::kForceSeparate;
    }
    return std::nullopt;
  }

  auto ParseShaderType(const std::string_view stage) -> std::optional<uint8_t>
  {
    using oxygen::ShaderType;
    if (stage == "amplification") {
      return static_cast<uint8_t>(ShaderType::kAmplification);
    }
    if (stage == "mesh") {
      return static_cast<uint8_t>(ShaderType::kMesh);
    }
    if (stage == "vertex") {
      return static_cast<uint8_t>(ShaderType::kVertex);
    }
    if (stage == "hull") {
      return static_cast<uint8_t>(ShaderType::kHull);
    }
    if (stage == "domain") {
      return static_cast<uint8_t>(ShaderType::kDomain);
    }
    if (stage == "geometry") {
      return static_cast<uint8_t>(ShaderType::kGeometry);
    }
    if (stage == "pixel") {
      return static_cast<uint8_t>(ShaderType::kPixel);
    }
    if (stage == "compute") {
      return static_cast<uint8_t>(ShaderType::kCompute);
    }
    if (stage == "ray_gen") {
      return static_cast<uint8_t>(ShaderType::kRayGen);
    }
    if (stage == "intersection") {
      return static_cast<uint8_t>(ShaderType::kIntersection);
    }
    if (stage == "any_hit") {
      return static_cast<uint8_t>(ShaderType::kAnyHit);
    }
    if (stage == "closest_hit") {
      return static_cast<uint8_t>(ShaderType::kClosestHit);
    }
    if (stage == "miss") {
      return static_cast<uint8_t>(ShaderType::kMiss);
    }
    if (stage == "callable") {
      return static_cast<uint8_t>(ShaderType::kCallable);
    }
    return std::nullopt;
  }

  template <size_t N>
  auto ParseFloatArray(const nlohmann::json& value, float (&out)[N]) noexcept
    -> bool
  {
    if (!value.is_array() || value.size() != N) {
      return false;
    }
    for (size_t i = 0; i < N; ++i) {
      if (!value[i].is_number()) {
        return false;
      }
      out[i] = value[i].get<float>();
    }
    return true;
  }

  auto ApplyMaterialInputsFromJson(
    const nlohmann::json& doc, MaterialInputs& inputs) -> bool
  {
    if (!doc.is_object()) {
      return false;
    }

    if (doc.contains("base_color")
      && !ParseFloatArray(doc.at("base_color"), inputs.base_color)) {
      return false;
    }
    if (doc.contains("normal_scale")) {
      inputs.normal_scale = doc.at("normal_scale").get<float>();
    }
    if (doc.contains("metalness")) {
      inputs.metalness = doc.at("metalness").get<float>();
    }
    if (doc.contains("roughness")) {
      inputs.roughness = doc.at("roughness").get<float>();
    }
    if (doc.contains("ambient_occlusion")) {
      inputs.ambient_occlusion = doc.at("ambient_occlusion").get<float>();
    }
    if (doc.contains("emissive_factor")
      && !ParseFloatArray(doc.at("emissive_factor"), inputs.emissive_factor)) {
      return false;
    }
    if (doc.contains("alpha_cutoff")) {
      inputs.alpha_cutoff = doc.at("alpha_cutoff").get<float>();
    }
    if (doc.contains("ior")) {
      inputs.ior = doc.at("ior").get<float>();
    }
    if (doc.contains("specular_factor")) {
      inputs.specular_factor = doc.at("specular_factor").get<float>();
    }
    if (doc.contains("sheen_color_factor")
      && !ParseFloatArray(
        doc.at("sheen_color_factor"), inputs.sheen_color_factor)) {
      return false;
    }
    if (doc.contains("clearcoat_factor")) {
      inputs.clearcoat_factor = doc.at("clearcoat_factor").get<float>();
    }
    if (doc.contains("clearcoat_roughness")) {
      inputs.clearcoat_roughness = doc.at("clearcoat_roughness").get<float>();
    }
    if (doc.contains("transmission_factor")) {
      inputs.transmission_factor = doc.at("transmission_factor").get<float>();
    }
    if (doc.contains("thickness_factor")) {
      inputs.thickness_factor = doc.at("thickness_factor").get<float>();
    }
    if (doc.contains("attenuation_color")
      && !ParseFloatArray(
        doc.at("attenuation_color"), inputs.attenuation_color)) {
      return false;
    }
    if (doc.contains("attenuation_distance")) {
      inputs.attenuation_distance = doc.at("attenuation_distance").get<float>();
    }
    if (doc.contains("double_sided")) {
      inputs.double_sided = doc.at("double_sided").get<bool>();
    }
    if (doc.contains("unlit")) {
      inputs.unlit = doc.at("unlit").get<bool>();
    }
    if (doc.contains("roughness_as_glossiness")) {
      inputs.roughness_as_glossiness
        = doc.at("roughness_as_glossiness").get<bool>();
    }

    return true;
  }

} // namespace

auto MaterialDescriptorImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting material descriptor job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  auto telemetry = ImportTelemetry {};
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };
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

  if (!Request().options.material_descriptor.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.request_invalid",
      "MaterialDescriptorImportJob requires options.material_descriptor");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid material descriptor request");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto descriptor_doc = nlohmann::json {};
  auto parse_exception = std::optional<std::string> {};
  try {
    descriptor_doc = nlohmann::json::parse(
      Request().options.material_descriptor->normalized_descriptor_json);
  } catch (const std::exception& ex) {
    parse_exception = ex.what();
  }

  if (parse_exception.has_value()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.request_invalid_json",
      "Normalized descriptor payload is invalid JSON: " + *parse_exception);
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid material descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!descriptor_doc.is_object()) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.request_invalid_json",
      "Normalized descriptor payload must be a JSON object");
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Invalid material descriptor payload");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (!ValidateDescriptorSchema(session, Request(), descriptor_doc)) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F,
      "Material descriptor schema validation failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto item = MaterialPipeline::WorkItem {};
  item.source_id = Request().source_path.string();
  item.request = Request();
  item.naming_service = observer_ptr { &GetNamingService() };
  item.stop_token = StopToken();

  if (Request().job_name.has_value() && !Request().job_name->empty()) {
    item.material_name = *Request().job_name;
  } else if (descriptor_doc.contains("name")) {
    item.material_name = descriptor_doc.at("name").get<std::string>();
  } else {
    item.material_name = Request().source_path.stem().string();
  }
  if (item.material_name.empty()) {
    item.material_name = "Material";
  }
  item.storage_material_name = item.material_name;

  if (descriptor_doc.contains("domain")) {
    const auto domain
      = ParseMaterialDomain(descriptor_doc.at("domain").get<std::string>());
    if (!domain.has_value()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.domain_invalid",
        "Material domain is invalid; expected known domain enum");
    } else {
      item.material_domain = *domain;
    }
  }

  if (descriptor_doc.contains("alpha_mode")) {
    const auto alpha_mode
      = ParseAlphaMode(descriptor_doc.at("alpha_mode").get<std::string>());
    if (!alpha_mode.has_value()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.alpha_mode_invalid",
        "Material alpha_mode is invalid; expected opaque/masked/blended");
    } else {
      item.alpha_mode = *alpha_mode;
    }
  }

  if (descriptor_doc.contains("orm_policy")) {
    const auto orm_policy
      = ParseOrmPolicy(descriptor_doc.at("orm_policy").get<std::string>());
    if (!orm_policy.has_value()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.orm_policy_invalid",
        "Material orm_policy is invalid; expected auto/force_*");
    } else {
      item.orm_policy = *orm_policy;
    }
  }

  if (descriptor_doc.contains("inputs")) {
    if (!ApplyMaterialInputsFromJson(
          descriptor_doc.at("inputs"), item.inputs)) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.inputs_invalid",
        "Material inputs payload has invalid shape");
    }
  }

  auto parse_failed = session.HasErrors();
  auto* const reader = FileReader().get();
  if (reader == nullptr) {
    AddDiagnostic(session, Request(), ImportSeverity::kError,
      "material.descriptor.reader_unavailable",
      "Async file reader is not available");
    parse_failed = true;
  }

  const auto resolve_texture_binding
    = [&](const nlohmann::json& textures_doc, const std::string_view slot_name,
        MaterialTextureBinding& binding) -> co::Co<> {
    if (!textures_doc.contains(slot_name)) {
      co_return;
    }

    const auto object_path = std::string("textures.") + std::string(slot_name);
    const auto& binding_doc = textures_doc.at(slot_name);
    if (!binding_doc.is_object()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.texture_binding_invalid",
        "Texture binding must be an object", object_path);
      parse_failed = true;
      co_return;
    }

    const auto virtual_path = binding_doc.at("virtual_path").get<std::string>();
    if (reader == nullptr) {
      parse_failed = true;
      co_return;
    }
    const auto resolved_index = co_await ResolveTextureIndexFromVirtualPath(
      session, Request(), *reader, virtual_path, object_path);
    if (!resolved_index.has_value()) {
      parse_failed = true;
      co_return;
    }

    binding.assigned = true;
    binding.source_id = virtual_path;
    binding.index = *resolved_index;

    if (binding_doc.contains("uv_set")) {
      binding.uv_set = binding_doc.at("uv_set").get<uint8_t>();
    }
    if (binding_doc.contains("uv_transform")) {
      const auto& uv_transform = binding_doc.at("uv_transform");
      if (uv_transform.contains("scale")
        && !ParseFloatArray(
          uv_transform.at("scale"), binding.uv_transform.scale)) {
        AddDiagnostic(session, Request(), ImportSeverity::kError,
          "material.descriptor.texture_uv_invalid",
          "uv_transform.scale must be [x,y]", object_path);
        parse_failed = true;
      }
      if (uv_transform.contains("offset")
        && !ParseFloatArray(
          uv_transform.at("offset"), binding.uv_transform.offset)) {
        AddDiagnostic(session, Request(), ImportSeverity::kError,
          "material.descriptor.texture_uv_invalid",
          "uv_transform.offset must be [x,y]", object_path);
        parse_failed = true;
      }
      if (uv_transform.contains("rotation_radians")) {
        binding.uv_transform.rotation_radians
          = uv_transform.at("rotation_radians").get<float>();
      }
    }
  };

  if (descriptor_doc.contains("textures")) {
    const auto& textures_doc = descriptor_doc.at("textures");
    if (!textures_doc.is_object()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.textures_invalid",
        "Material textures payload must be an object");
      parse_failed = true;
    } else {
      for (const auto& entry : kTextureSlotBindings) {
        co_await resolve_texture_binding(
          textures_doc, entry.slot_name, item.textures.*(entry.binding_member));
      }
    }
  }

  if (descriptor_doc.contains("shaders")) {
    const auto& shaders_doc = descriptor_doc.at("shaders");
    if (!shaders_doc.is_array()) {
      AddDiagnostic(session, Request(), ImportSeverity::kError,
        "material.descriptor.shaders_invalid",
        "Material shaders payload must be an array");
      parse_failed = true;
    } else {
      for (size_t i = 0; i < shaders_doc.size(); ++i) {
        const auto& stage_doc = shaders_doc[i];
        const auto object_path = "shaders[" + std::to_string(i) + "]";
        if (!stage_doc.is_object()) {
          AddDiagnostic(session, Request(), ImportSeverity::kError,
            "material.descriptor.shaders_invalid",
            "Shader stage entry must be an object", object_path);
          parse_failed = true;
          continue;
        }

        const auto shader_type
          = ParseShaderType(stage_doc.at("stage").get<std::string>());
        if (!shader_type.has_value()) {
          AddDiagnostic(session, Request(), ImportSeverity::kError,
            "material.descriptor.shader_stage_invalid",
            "Shader stage is not recognized", object_path);
          parse_failed = true;
          continue;
        }

        auto shader = ShaderRequest {};
        shader.shader_type = *shader_type;
        shader.source_path = stage_doc.at("source_path").get<std::string>();
        shader.entry_point = stage_doc.at("entry_point").get<std::string>();
        if (stage_doc.contains("defines")) {
          shader.defines = stage_doc.at("defines").get<std::string>();
        }
        if (stage_doc.contains("shader_hash")) {
          shader.shader_hash = stage_doc.at("shader_hash").get<uint64_t>();
        }
        item.shader_requests.push_back(std::move(shader));
      }
    }
  }

  if (parse_failed || session.HasErrors()) {
    ReportPhaseProgress(
      ImportPhase::kFailed, 1.0F, "Material descriptor parse failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(
    ImportPhase::kWorking, 0.5F, "Importing material descriptor...");
  auto pipeline = MaterialPipeline(*ThreadPool(),
    MaterialPipeline::Config {
      .queue_capacity = Concurrency().material.queue_capacity,
      .worker_count = Concurrency().material.workers,
      .with_content_hashing
      = EffectiveContentHashingEnabled(Request().options.with_content_hashing),
    });
  StartPipeline(pipeline);

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
      ImportPhase::kFailed, 1.0F, "Material descriptor import failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  const auto emit_start = std::chrono::steady_clock::now();
  auto& cooked = *result.cooked;
  session.AssetEmitter().Emit(cooked.material_key, data::AssetType::kMaterial,
    cooked.virtual_path, cooked.descriptor_relpath, cooked.descriptor_bytes);
  session.AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto MaterialDescriptorImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
