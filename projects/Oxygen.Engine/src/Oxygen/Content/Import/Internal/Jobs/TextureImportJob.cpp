//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/ImageDecode.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/Jobs/TextureImportJob.h>
#include <Oxygen/Content/Import/Internal/Jobs/TextureImportPolicy.h>
#include <Oxygen/Content/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/Internal/TextureSourceAssembly_internal.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/OxCo/Algorithms.h>

namespace {

using oxygen::Format;
using oxygen::TextureType;
using oxygen::co::AllOf;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::CubeFace;
using oxygen::content::import::CubeMapImageLayout;
using oxygen::content::import::DetectCubeMapLayout;
using oxygen::content::import::ImportOptions;
using oxygen::content::import::ImportTelemetry;
using oxygen::content::import::IsHdrFormat;
using oxygen::content::import::kCubeFaceCount;
using oxygen::content::import::MipFilter;
using oxygen::content::import::ScratchImage;
using oxygen::content::import::ScratchImageMeta;
using oxygen::content::import::TextureImportDesc;
using oxygen::content::import::TextureImportError;
using oxygen::content::import::TextureIntent;

auto IsColorIntent(const TextureIntent intent) -> bool
{
  switch (intent) {
  case TextureIntent::kAlbedo:
  case TextureIntent::kEmissive:
    return true;
  case TextureIntent::kNormalTS:
  case TextureIntent::kRoughness:
  case TextureIntent::kMetallic:
  case TextureIntent::kAO:
  case TextureIntent::kOpacity:
  case TextureIntent::kORMPacked:
  case TextureIntent::kHdrEnvironment:
  case TextureIntent::kHdrLightProbe:
  case TextureIntent::kData:
  case TextureIntent::kHeightMap:
    return false;
  }

  return false;
}

[[nodiscard]] auto IsBc7Format(const Format format) noexcept -> bool
{
  return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
}

[[nodiscard]] auto NormalizeBc7Quality(
  const Format format, const Bc7Quality quality) noexcept -> Bc7Quality
{
  return IsBc7Format(format) ? quality : Bc7Quality::kNone;
}

[[nodiscard]] auto NormalizeTextureId(const std::filesystem::path& source_path)
  -> std::string
{
  auto normalized = source_path.lexically_normal();
  normalized.make_preferred();
  return normalized.generic_string();
}

[[nodiscard]] auto BuildPreflightDesc(
  const ImportOptions::TextureTuning& tuning, const bool is_hdr_input,
  const bool is_cubemap) -> TextureImportDesc
{
  TextureImportDesc desc {};
  desc.texture_type
    = is_cubemap ? TextureType::kTextureCube : TextureType::kTexture2D;
  desc.width = 1;
  desc.height = 1;
  desc.depth = 1;
  desc.array_layers = is_cubemap ? kCubeFaceCount : 1;
  desc.intent = tuning.intent;
  desc.source_color_space = tuning.source_color_space;
  desc.flip_y_on_decode = tuning.flip_y_on_decode;
  desc.force_rgba_on_decode = tuning.force_rgba_on_decode;

  if (tuning.enabled) {
    desc.mip_policy = tuning.mip_policy;
    desc.max_mip_levels = tuning.max_mip_levels;
    desc.mip_filter = tuning.mip_filter;
    desc.output_format = IsColorIntent(desc.intent) ? tuning.color_output_format
                                                    : tuning.data_output_format;
    desc.bc7_quality
      = NormalizeBc7Quality(desc.output_format, tuning.bc7_quality);
  } else {
    desc.output_format = Format::kRGBA8UNorm;
    desc.bc7_quality = Bc7Quality::kNone;
  }

  if (desc.intent == TextureIntent::kHdrEnvironment
    || desc.intent == TextureIntent::kHdrLightProbe) {
    const bool is_float_output = desc.output_format == Format::kRGBA16Float
      || desc.output_format == Format::kRGBA32Float
      || desc.output_format == Format::kR11G11B10Float;
    if (!is_float_output && !is_hdr_input) {
      desc.bake_hdr_to_ldr = true;
    }
  }

  return desc;
}

[[nodiscard]] auto ValidatePreflight(const ImportOptions::TextureTuning& tuning,
  std::span<const std::byte> bytes, std::string_view extension,
  const bool is_cubemap) -> std::optional<TextureImportError>
{
  const bool is_hdr_input = IsHdrFormat(bytes, extension);
  auto desc = BuildPreflightDesc(tuning, is_hdr_input, is_cubemap);
  return desc.Validate();
}

} // namespace

namespace oxygen::content::import::detail {

/*!
 Execute a standalone texture import workflow.

 The current implementation wires the job lifecycle and progress reporting.
 Phase 5 will populate the load/cook/emit stages with real pipeline work.
*/
auto TextureImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "TextureImportJob starting: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  ImportTelemetry telemetry {};
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
    report.telemetry = telemetry;
    co_return report;
  };

  {
    const auto& tuning = Request().options.texture_tuning;
    DLOG_SCOPE_F(INFO, "TextureImportJob tuning");
    DLOG_F(INFO, "  enabled: {}", tuning.enabled);
    DLOG_F(INFO, "  intent: {}", tuning.intent);
    DLOG_F(INFO, "  color_space: {}", tuning.source_color_space);
    DLOG_F(INFO, "  output_format: {}", tuning.color_output_format);
    DLOG_F(INFO, "  data_format: {}", tuning.data_output_format);
    DLOG_F(INFO, "  mip_policy: {}", tuning.mip_policy);
    DLOG_F(INFO, "  mip_filter: {}", tuning.mip_filter);
    DLOG_F(INFO, "  bc7_quality: {}", tuning.bc7_quality);
    DLOG_F(INFO, "  max_mips: {}", tuning.max_mip_levels);
    DLOG_F(INFO, "  packing_policy: {}", tuning.packing_policy_id);
    DLOG_F(INFO, "  cubemap: {}", tuning.import_cubemap);
    DLOG_F(INFO, "  equirect_to_cube: {}", tuning.equirect_to_cubemap);
    DLOG_F(INFO, "  cube_face_size: {}", tuning.cubemap_face_size);
    DLOG_F(INFO, "  cube_layout: {}", tuning.cubemap_layout);
    DLOG_F(INFO, "  flip_y: {}", tuning.flip_y_on_decode);
    DLOG_F(INFO, "  force_rgba: {}", tuning.force_rgba_on_decode);
  }

  EnsureCookedRoot();

  ImportSession session(
    Request(), FileReader(), FileWriter(), ThreadPool(), TableRegistry());

  TexturePipeline pipeline(*ThreadPool(),
    TexturePipeline::Config {
      .with_content_hashing = Request().options.with_content_hashing,
    });
  StartPipeline(pipeline);

  ReportProgress(
    ImportPhase::kParsing, 0.0f, 0.0f, 0U, 0U, "Loading texture source...");
  const auto load_start = std::chrono::steady_clock::now();
  auto source = co_await LoadSource(session);
  const auto load_end = std::chrono::steady_clock::now();
  telemetry.load_duration = MakeDuration(load_start, load_end);
  telemetry.io_duration = source.io_duration;
  telemetry.decode_duration = source.decode_duration;
  if (!source.success) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "Texture load failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (source.meta.has_value()) {
    const auto& meta = source.meta.value();
    DLOG_SCOPE_F(INFO, "Texture source prepared");
    DLOG_F(INFO, "  type: {}", meta.texture_type);
    DLOG_F(INFO, "  format: {}", meta.format);
    DLOG_F(INFO, "  size: {}x{}", meta.width, meta.height);
    DLOG_F(INFO, "  depth: {}", meta.depth);
    DLOG_F(INFO, "  layers: {}", meta.array_layers);
    DLOG_F(INFO, "  mips: {}", meta.mip_levels);
    if (source.source_set.has_value()) {
      DLOG_F(INFO, "  sources: {}", source.source_set->Count());
    }
  }

  ReportProgress(
    ImportPhase::kTextures, 0.4f, 0.0f, 0U, 0U, "Cooking texture...");
  const auto cook_start = std::chrono::steady_clock::now();
  auto cooked = co_await CookTexture(source, session, pipeline);
  const auto cook_end = std::chrono::steady_clock::now();
  telemetry.cook_duration = MakeDuration(cook_start, cook_end);
  if (cooked.decode_duration.has_value()) {
    telemetry.decode_duration = cooked.decode_duration;
  }
  if (!cooked.payload.has_value() && !cooked.used_fallback) {
    ReportProgress(
      ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "Texture cook failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  if (cooked.payload.has_value()) {
    ReportProgress(
      ImportPhase::kWriting, 0.7f, 0.0f, 0U, 0U, "Emitting texture...");
    const auto emit_start = std::chrono::steady_clock::now();
    if (!co_await EmitTexture(std::move(*cooked.payload), session)) {
      ReportProgress(
        ImportPhase::kFailed, 1.0f, 1.0f, 0U, 0U, "Texture emit failed");
      co_return co_await FinalizeWithTelemetry(session);
    }
    const auto emit_end = std::chrono::steady_clock::now();
    telemetry.emit_duration = MakeDuration(emit_start, emit_end);
  }

  ReportProgress(
    ImportPhase::kWriting, 0.9f, 0.0f, 0U, 0U, "Finalizing import...");
  auto report = co_await FinalizeWithTelemetry(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, 1.0f, 0U, 0U, report.success ? "Import complete" : "Import failed");

  co_return report;
}

//! Load the texture bytes from disk or embedded data.
auto TextureImportJob::LoadSource(ImportSession& session)
  -> co::Co<TextureSource>
{
  TextureSource source {};
  source.source_id = Request().source_path.string();

  std::chrono::microseconds io_duration { 0 };
  std::chrono::microseconds decode_duration { 0 };
  const auto AddDuration = [](std::chrono::microseconds& total,
                             const std::chrono::steady_clock::time_point start,
                             const std::chrono::steady_clock::time_point end) {
    total += std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };
  const auto StampDurations = [&](TextureSource& target) -> TextureSource&& {
    if (io_duration.count() > 0) {
      target.io_duration = io_duration;
    }
    if (decode_duration.count() > 0) {
      target.decode_duration = decode_duration;
    }
    return std::move(target);
  };

  auto reader = FileReader();
  if (reader == nullptr) {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.reader_missing",
      .message = "Async file reader is not available",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return StampDurations(source);
  }

  const auto& tuning = Request().options.texture_tuning;
  DecodeOptions options {};
  options.flip_y = tuning.flip_y_on_decode;
  options.force_rgba = tuning.force_rgba_on_decode;

  const bool import_cubemap = tuning.import_cubemap
    || tuning.equirect_to_cubemap
    || tuning.cubemap_layout != CubeMapImageLayout::kUnknown;
  if (import_cubemap) {
    if (tuning.equirect_to_cubemap) {
      if (tuning.cubemap_face_size == 0U) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.cubemap_face_size_missing",
          .message = "Cubemap face size is required for equirect conversion",
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      if ((tuning.cubemap_face_size % 256U) != 0U) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.cubemap_face_size_invalid",
          .message = "Cubemap face size must be a multiple of 256",
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      options.extension_hint = Request().source_path.extension().string();
      const auto read_start = std::chrono::steady_clock::now();
      auto read_result = co_await reader.get()->ReadFile(Request().source_path);
      const auto read_end = std::chrono::steady_clock::now();
      AddDuration(io_duration, read_start, read_end);
      if (!read_result.has_value()) {
        const auto& error = read_result.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.read_failed",
          .message = error.ToString(),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      auto bytes = std::move(read_result.value());
      if (const auto error
        = ValidatePreflight(tuning, bytes, options.extension_hint, true)) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.desc_invalid",
          .message
          = std::string("Invalid texture descriptor: ") + to_string(*error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }
      source.prevalidated = true;
      source.is_hdr_input = IsHdrFormat(bytes, options.extension_hint);
      source.bytes = std::make_shared<std::vector<std::byte>>(std::move(bytes));
      source.success = true;
      co_return StampDurations(source);
    }

    const bool wants_layout
      = tuning.cubemap_layout != CubeMapImageLayout::kUnknown;

    if (wants_layout) {
      options.extension_hint = Request().source_path.extension().string();
      const auto read_start = std::chrono::steady_clock::now();
      auto read_result = co_await reader.get()->ReadFile(Request().source_path);
      const auto read_end = std::chrono::steady_clock::now();
      AddDuration(io_duration, read_start, read_end);
      if (!read_result.has_value()) {
        const auto& error = read_result.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.read_failed",
          .message = error.ToString(),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      auto bytes = std::move(read_result.value());
      if (const auto error
        = ValidatePreflight(tuning, bytes, options.extension_hint, true)) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.desc_invalid",
          .message
          = std::string("Invalid texture descriptor: ") + to_string(*error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }
      source.prevalidated = true;
      source.is_hdr_input = IsHdrFormat(bytes, options.extension_hint);
      source.bytes = std::make_shared<std::vector<std::byte>>(std::move(bytes));
      source.success = true;
      co_return StampDurations(source);
    }

    auto discovered = DiscoverCubeFacePaths(Request().source_path);
    if (!discovered.has_value()) {
      session.AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "texture.cubemap_faces_missing",
        .message = "Cubemap faces could not be discovered",
        .source_path = Request().source_path.string(),
        .object_path = {},
      });
      co_return StampDurations(source);
    }

    TextureSourceSet sources;

    for (size_t i = 0; i < kCubeFaceCount; ++i) {
      const auto& face_path = (*discovered)[i];
      const auto read_start = std::chrono::steady_clock::now();
      auto read_result = co_await reader.get()->ReadFile(face_path);
      const auto read_end = std::chrono::steady_clock::now();
      AddDuration(io_duration, read_start, read_end);
      if (!read_result.has_value()) {
        const auto& error = read_result.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.read_failed",
          .message = error.ToString(),
          .source_path = face_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      auto bytes = std::move(read_result.value());
      if (const auto error = ValidatePreflight(
            tuning, bytes, face_path.extension().string(), true)) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.desc_invalid",
          .message
          = std::string("Invalid texture descriptor: ") + to_string(*error),
          .source_path = face_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }
      source.prevalidated = true;
      if (!source.is_hdr_input.has_value()) {
        source.is_hdr_input
          = IsHdrFormat(bytes, face_path.extension().string());
      }

      sources.AddCubeFace(
        static_cast<CubeFace>(i), std::move(bytes), face_path.string());
    }

    source.source_set = std::move(sources);
    source.success = true;
    co_return StampDurations(source);
  }

  const auto read_start = std::chrono::steady_clock::now();
  auto read_result = co_await reader.get()->ReadFile(Request().source_path);
  const auto read_end = std::chrono::steady_clock::now();
  AddDuration(io_duration, read_start, read_end);
  if (!read_result.has_value()) {
    const auto& error = read_result.error();
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.read_failed",
      .message = error.ToString(),
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return StampDurations(source);
  }

  auto bytes = std::move(read_result.value());
  options.extension_hint = Request().source_path.extension().string();
  if (const auto error
    = ValidatePreflight(tuning, bytes, options.extension_hint, false)) {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.desc_invalid",
      .message
      = std::string("Invalid texture descriptor: ") + to_string(*error),
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return StampDurations(source);
  }
  source.prevalidated = true;
  source.is_hdr_input = IsHdrFormat(bytes, options.extension_hint);
  source.bytes = std::make_shared<std::vector<std::byte>>(std::move(bytes));
  source.success = true;
  co_return StampDurations(source);
}

//! Cook the texture via the async TexturePipeline.
auto TextureImportJob::CookTexture(
  TextureSource& source, ImportSession& session, TexturePipeline& pipeline)
  -> co::Co<CookedTextureResult>
{
  const auto& tuning = Request().options.texture_tuning;
  const bool has_meta = source.meta.has_value();
  const bool is_cubemap = source.source_set.has_value()
    || (has_meta && source.meta->texture_type == TextureType::kTextureCube)
    || tuning.import_cubemap || tuning.equirect_to_cubemap
    || tuning.cubemap_layout != CubeMapImageLayout::kUnknown;

  TextureImportDesc desc {};
  if (has_meta) {
    const auto& meta = source.meta.value();
    desc.source_id = source.source_id;
    desc.texture_type = meta.texture_type;
    desc.width = meta.width;
    desc.height = meta.height;
    desc.depth = meta.depth;
    desc.array_layers = meta.array_layers;
    desc.intent = tuning.intent;
    desc.source_color_space = tuning.source_color_space;
    desc.flip_y_on_decode = tuning.flip_y_on_decode;
    desc.force_rgba_on_decode = tuning.force_rgba_on_decode;
    if (tuning.enabled) {
      desc.mip_policy = tuning.mip_policy;
      desc.max_mip_levels = tuning.max_mip_levels;
      desc.mip_filter = tuning.mip_filter;
      desc.output_format = IsColorIntent(desc.intent)
        ? tuning.color_output_format
        : tuning.data_output_format;
      desc.bc7_quality
        = NormalizeBc7Quality(desc.output_format, tuning.bc7_quality);
    } else {
      desc.output_format = meta.format;
      desc.bc7_quality = Bc7Quality::kNone;
    }

    if (desc.intent == TextureIntent::kHdrEnvironment
      || desc.intent == TextureIntent::kHdrLightProbe) {
      const bool is_float_output = desc.output_format == Format::kRGBA16Float
        || desc.output_format == Format::kRGBA32Float
        || desc.output_format == Format::kR11G11B10Float;
      if (!is_float_output && meta.format != Format::kRGBA32Float) {
        desc.bake_hdr_to_ldr = true;
      }
    }
  } else {
    desc = BuildPreflightDesc(
      tuning, source.is_hdr_input.value_or(false), is_cubemap);
    desc.source_id = source.source_id;
    desc.width = 0;
    desc.height = 0;
    desc.depth = 1;
    desc.array_layers = is_cubemap ? kCubeFaceCount : 1;
  }

  {
    DLOG_SCOPE_F(INFO, "Texture descriptor");
    DLOG_F(INFO, "  type: {}", desc.texture_type);
    DLOG_F(INFO, "  intent: {}", desc.intent);
    DLOG_F(INFO, "  color_space: {}", desc.source_color_space);
    DLOG_F(INFO, "  size: {}x{}", desc.width, desc.height);
    DLOG_F(INFO, "  depth: {}", desc.depth);
    DLOG_F(INFO, "  layers: {}", desc.array_layers);
    DLOG_F(INFO, "  output_format: {}", desc.output_format);
    DLOG_F(INFO, "  mip_policy: {}", desc.mip_policy);
    DLOG_F(INFO, "  max_mips: {}", desc.max_mip_levels);
    DLOG_F(INFO, "  mip_filter: {}", desc.mip_filter);
    DLOG_F(INFO, "  packing: {}",
      tuning.enabled ? tuning.packing_policy_id : "d3d12");
  }

  if (!source.prevalidated) {
    if (const auto error = desc.Validate()) {
      session.AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "texture.desc_invalid",
        .message
        = std::string("Invalid texture descriptor: ") + to_string(*error),
        .source_path = Request().source_path.string(),
        .object_path = {},
      });
      DLOG_F(ERROR, "Texture descriptor validation failed: {}", *error);
      co_return CookedTextureResult {};
    }
  }

  const auto texture_id = NormalizeTextureId(Request().source_path);

  TexturePipeline::WorkItem item {};
  item.source_id = source.source_id;
  item.texture_id = texture_id.empty() ? source.source_id : texture_id;
  item.desc = desc;
  item.packing_policy_id = tuning.enabled ? tuning.packing_policy_id : "d3d12";
  item.output_format_is_override = tuning.enabled;
  item.failure_policy = FailurePolicyForTextureTuning(tuning);
  item.equirect_to_cubemap = tuning.equirect_to_cubemap;
  item.cubemap_face_size = tuning.cubemap_face_size;
  item.cubemap_layout = tuning.cubemap_layout;
  if (source.source_set.has_value()) {
    item.source = std::move(source.source_set.value());
  } else if (source.bytes != nullptr) {
    TexturePipeline::SourceBytes raw_source {
      .bytes = std::span<const std::byte>(*source.bytes),
      .owner = std::static_pointer_cast<const void>(source.bytes),
    };
    item.source = std::move(raw_source);
  } else if (source.image.has_value()) {
    item.source = std::move(source.image.value());
  } else {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.source_missing",
      .message = "Texture source data is missing",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return CookedTextureResult {};
  }
  item.stop_token = StopToken();

  co_await pipeline.Submit(std::move(item));
  pipeline.Close();

  auto result = co_await pipeline.Collect();
  for (const auto& diagnostic : result.diagnostics) {
    session.AddDiagnostic(diagnostic);
  }
  if (result.used_placeholder) {
    session.AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code = "texture.placeholder_used",
      .message = "Texture cook failed; used placeholder payload",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
  }

  if (!result.success || !result.cooked.has_value()) {
    if (result.used_placeholder) {
      session.AddDiagnostic({
        .severity = ImportSeverity::kWarning,
        .code = "texture.placeholder_used",
        .message = "Texture cooking failed; using fallback texture",
        .source_path = Request().source_path.string(),
        .object_path = {},
      });
      (void)session.TextureEmitter();
      co_return { .payload = std::nullopt,
        .decode_duration = result.decode_duration,
        .used_fallback = true };
    }

    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.cook_failed",
      .message = "Texture pipeline did not return a cooked payload",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return { .payload = std::nullopt,
      .decode_duration = result.decode_duration,
      .used_fallback = false };
  }

  co_return { .payload = std::move(result.cooked.value()),
    .decode_duration = result.decode_duration,
    .used_fallback = false };
}

//! Emit the cooked texture via TextureEmitter.
auto TextureImportJob::EmitTexture(
  CookedTexturePayload cooked, ImportSession& session) -> co::Co<bool>
{
  try {
    auto& emitter = session.TextureEmitter();
    const auto index = emitter.Emit(std::move(cooked));
    DLOG_F(INFO, "Texture emitted at index={}", index);
    co_return true;
  } catch (const std::exception& ex) {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.emit_failed",
      .message = std::string("Texture emission failed: ") + ex.what(),
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return false;
  }
}

//! Finalize the session and return the import report.
auto TextureImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
