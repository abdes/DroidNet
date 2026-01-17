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
#include <Oxygen/Content/Import/Async/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Async/IAsyncFileReader.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/Jobs/TextureImportJob.h>
#include <Oxygen/Content/Import/Async/Jobs/TextureImportPolicy.h>
#include <Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Internal/TextureSourceAssemblyInternal.h>
#include <Oxygen/Content/Import/TextureImportDesc.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <glm/gtc/packing.hpp>

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
using oxygen::content::import::detail::ConvertEquirectangularFace;
using oxygen::content::import::detail::ExtractCubeFaceFromLayout;
using oxygen::content::import::detail::GetBytesPerPixel;

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

[[nodiscard]] auto NormalizeTextureId(const std::filesystem::path& source_path)
  -> std::string
{
  auto normalized = source_path.lexically_normal();
  normalized.make_preferred();
  return normalized.generic_string();
}

[[nodiscard]] auto ConvertToFloatImage(ScratchImage&& image)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (!image.IsValid()) {
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  const auto& meta = image.Meta();
  if (meta.format == Format::kRGBA32Float) {
    return ::oxygen::Ok(std::move(image));
  }

  if (meta.format != Format::kRGBA8UNorm
    && meta.format != Format::kRGBA8UNormSRGB) {
    return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
  }

  ScratchImage float_image = ScratchImage::Create(ScratchImageMeta {
    .texture_type = TextureType::kTexture2D,
    .width = meta.width,
    .height = meta.height,
    .depth = 1,
    .array_layers = 1,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  });

  if (!float_image.IsValid()) {
    return ::oxygen::Err(TextureImportError::kOutOfMemory);
  }

  const auto src_view = image.GetImage(0, 0);
  auto dst_pixels = float_image.GetMutablePixels(0, 0);
  const auto* src_ptr = src_view.pixels.data();
  auto* dst_ptr = reinterpret_cast<float*>(dst_pixels.data());

  const size_t pixel_count
    = static_cast<size_t>(meta.width) * static_cast<size_t>(meta.height);
  for (size_t i = 0; i < pixel_count; ++i) {
    for (size_t c = 0; c < 4; ++c) {
      const uint8_t byte_val = static_cast<uint8_t>(src_ptr[i * 4 + c]);
      dst_ptr[i * 4 + c] = static_cast<float>(byte_val) / 255.0F;
    }
  }

  return ::oxygen::Ok(std::move(float_image));
}

[[nodiscard]] auto ConvertFloat32ToFloat16Image(const ScratchImage& source)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (!source.IsValid()) {
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  const auto& meta = source.Meta();
  if (meta.format != Format::kRGBA32Float) {
    return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
  }

  ScratchImage result = ScratchImage::Create(ScratchImageMeta {
    .texture_type = meta.texture_type,
    .width = meta.width,
    .height = meta.height,
    .depth = meta.depth,
    .array_layers = meta.array_layers,
    .mip_levels = meta.mip_levels,
    .format = Format::kRGBA16Float,
  });

  if (!result.IsValid()) {
    return ::oxygen::Err(TextureImportError::kOutOfMemory);
  }

  for (uint16_t layer = 0; layer < meta.array_layers; ++layer) {
    for (uint16_t mip = 0; mip < meta.mip_levels; ++mip) {
      const auto src_view = source.GetImage(layer, mip);
      auto dst_pixels = result.GetMutablePixels(layer, mip);

      const auto* src_ptr
        = reinterpret_cast<const float*>(src_view.pixels.data());
      auto* dst_ptr = reinterpret_cast<uint16_t*>(dst_pixels.data());

      const size_t pixel_count
        = static_cast<size_t>(src_view.width) * src_view.height;
      for (size_t i = 0; i < pixel_count * 4; ++i) {
        dst_ptr[i] = glm::packHalf1x16(src_ptr[i]);
      }
    }
  }

  return ::oxygen::Ok(std::move(result));
}

[[nodiscard]] auto WantsHalfFloatOutput(
  const ImportOptions::TextureTuning& tuning) -> bool
{
  if (!tuning.enabled) {
    return false;
  }

  const auto output_format = IsColorIntent(tuning.intent)
    ? tuning.color_output_format
    : tuning.data_output_format;

  return output_format == Format::kRGBA16Float;
}

[[nodiscard]] auto ConvertEquirectangularToCubeOnThreadPool(
  oxygen::co::ThreadPool& thread_pool, const ScratchImage& equirect,
  const oxygen::content::import::EquirectToCubeOptions& options)
  -> oxygen::co::Co<oxygen::Result<ScratchImage, TextureImportError>>
{
  if (!equirect.IsValid()) {
    co_return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  const auto& src_meta = equirect.Meta();
  const float aspect
    = static_cast<float>(src_meta.width) / static_cast<float>(src_meta.height);
  if (aspect < 1.5F || aspect > 2.5F) {
    co_return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  if (src_meta.format != Format::kRGBA32Float) {
    co_return ::oxygen::Err(TextureImportError::kInvalidOutputFormat);
  }

  if (options.face_size == 0) {
    co_return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  ScratchImageMeta cube_meta {
    .texture_type = TextureType::kTextureCube,
    .width = options.face_size,
    .height = options.face_size,
    .depth = 1,
    .array_layers = kCubeFaceCount,
    .mip_levels = 1,
    .format = Format::kRGBA32Float,
  };

  ScratchImage cube = ScratchImage::Create(cube_meta);
  if (!cube.IsValid()) {
    co_return ::oxygen::Err(TextureImportError::kOutOfMemory);
  }

  const auto src_view = equirect.GetImage(0, 0);
  const bool use_bicubic = (options.sample_filter == MipFilter::kKaiser
    || options.sample_filter == MipFilter::kLanczos);
  const uint32_t face_size = options.face_size;

  std::vector<oxygen::co::Co<>> jobs;
  jobs.reserve(kCubeFaceCount);

  for (uint32_t face_idx = 0; face_idx < kCubeFaceCount; ++face_idx) {
    const auto face = static_cast<CubeFace>(face_idx);
    jobs.push_back([&](const CubeFace face_value) -> oxygen::co::Co<> {
      co_await thread_pool.Run([&]() {
        ConvertEquirectangularFace(equirect, src_meta, src_view.pixels,
          face_value, face_size, use_bicubic, cube);
      });
    }(face));
  }

  co_await AllOf(std::move(jobs));
  co_return ::oxygen::Ok(std::move(cube));
}

[[nodiscard]] auto ExtractCubeFacesFromLayoutOnThreadPool(
  oxygen::co::ThreadPool& thread_pool, const ScratchImage& layout_image,
  const CubeMapImageLayout layout)
  -> oxygen::co::Co<oxygen::Result<ScratchImage, TextureImportError>>
{
  if (!layout_image.IsValid()) {
    co_return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  if (layout == CubeMapImageLayout::kAuto) {
    const auto detection = DetectCubeMapLayout(layout_image);
    if (!detection.has_value()) {
      co_return ::oxygen::Err(TextureImportError::kDimensionMismatch);
    }
    co_return co_await ExtractCubeFacesFromLayoutOnThreadPool(
      thread_pool, layout_image, detection->layout);
  }

  if (layout == CubeMapImageLayout::kUnknown) {
    co_return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  const auto& meta = layout_image.Meta();
  const auto detection = DetectCubeMapLayout(meta.width, meta.height);
  if (!detection.has_value() || detection->layout != layout) {
    co_return ::oxygen::Err(TextureImportError::kDimensionMismatch);
  }

  const uint32_t face_size = detection->face_size;
  const std::size_t bytes_per_pixel = GetBytesPerPixel(meta.format);
  if (bytes_per_pixel == 0) {
    co_return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
  }

  ScratchImageMeta cube_meta {
    .texture_type = TextureType::kTextureCube,
    .width = face_size,
    .height = face_size,
    .depth = 1,
    .array_layers = kCubeFaceCount,
    .mip_levels = 1,
    .format = meta.format,
  };

  ScratchImage cube = ScratchImage::Create(cube_meta);
  if (!cube.IsValid()) {
    co_return ::oxygen::Err(TextureImportError::kOutOfMemory);
  }

  const auto src_view = layout_image.GetImage(0, 0);
  std::vector<oxygen::co::Co<>> jobs;
  jobs.reserve(kCubeFaceCount);

  for (uint32_t face_idx = 0; face_idx < kCubeFaceCount; ++face_idx) {
    const auto face = static_cast<CubeFace>(face_idx);
    jobs.push_back([&](const CubeFace face_value) -> oxygen::co::Co<> {
      co_await thread_pool.Run([&]() {
        ExtractCubeFaceFromLayout(
          src_view, layout, face_size, bytes_per_pixel, face_value, cube);
      });
    }(face));
  }

  co_await AllOf(std::move(jobs));
  co_return ::oxygen::Ok(std::move(cube));
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
    desc.bc7_quality = tuning.bc7_quality;
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

  TexturePipeline pipeline(*ThreadPool());
  StartPipeline(pipeline);

  ReportProgress(ImportPhase::kParsing, 0.0f, "Loading texture source...");
  const auto load_start = std::chrono::steady_clock::now();
  auto source = co_await LoadSource(session);
  const auto load_end = std::chrono::steady_clock::now();
  telemetry.load_duration = MakeDuration(load_start, load_end);
  telemetry.io_duration = source.io_duration;
  telemetry.decode_duration = source.decode_duration;
  if (!source.success) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture load failed");
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

  ReportProgress(ImportPhase::kTextures, 0.4f, "Cooking texture...");
  const auto cook_start = std::chrono::steady_clock::now();
  auto cooked = co_await CookTexture(source, session, pipeline);
  const auto cook_end = std::chrono::steady_clock::now();
  telemetry.cook_duration = MakeDuration(cook_start, cook_end);
  if (!cooked.has_value()) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture cook failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportProgress(ImportPhase::kWriting, 0.7f, "Emitting texture...");
  const auto emit_start = std::chrono::steady_clock::now();
  if (!co_await EmitTexture(std::move(*cooked), session)) {
    ReportProgress(ImportPhase::kFailed, 1.0f, "Texture emit failed");
    co_return co_await FinalizeWithTelemetry(session);
  }
  const auto emit_end = std::chrono::steady_clock::now();
  telemetry.emit_duration = MakeDuration(emit_start, emit_end);

  ReportProgress(ImportPhase::kWriting, 0.9f, "Finalizing import...");
  auto report = co_await FinalizeWithTelemetry(session);

  ReportProgress(report.success ? ImportPhase::kComplete : ImportPhase::kFailed,
    1.0f, report.success ? "Import complete" : "Import failed");

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
      const auto source_id = Request().source_path.string();
      const auto decode_start = std::chrono::steady_clock::now();
      auto decoded = co_await ThreadPool()->Run(
        [data = bytes.data(), size = bytes.size(), options, source_id](
          oxygen::co::ThreadPool::CancelToken cancelled)
          -> oxygen::Result<ScratchImage, TextureImportError> {
          if (cancelled) {
            return ::oxygen::Err(TextureImportError::kCancelled);
          }
          auto result = DecodeToScratchImage(
            std::span<const std::byte>(data, size), options);
          return result;
        });
      const auto decode_end = std::chrono::steady_clock::now();
      AddDuration(decode_duration, decode_start, decode_end);
      if (!decoded.has_value()) {
        const auto error = decoded.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.decode_failed",
          .message = std::string("Decode failed: ") + to_string(error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      const auto float_start = std::chrono::steady_clock::now();
      auto float_image = ConvertToFloatImage(std::move(decoded.value()));
      const auto float_end = std::chrono::steady_clock::now();
      AddDuration(decode_duration, float_start, float_end);
      if (!float_image.has_value()) {
        const auto error = float_image.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.equirect_float_failed",
          .message
          = std::string("Equirect to float failed: ") + to_string(error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      EquirectToCubeOptions cube_options {
        .face_size = tuning.cubemap_face_size,
        .sample_filter = tuning.mip_filter,
      };

      const auto cube_start = std::chrono::steady_clock::now();
      auto cube = co_await ConvertEquirectangularToCubeOnThreadPool(
        *ThreadPool(), float_image.value(), cube_options);
      const auto cube_end = std::chrono::steady_clock::now();
      AddDuration(decode_duration, cube_start, cube_end);
      if (!cube.has_value()) {
        const auto error = cube.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.equirect_convert_failed",
          .message
          = std::string("Equirect conversion failed: ") + to_string(error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      if (WantsHalfFloatOutput(tuning)
        && cube->Meta().format == Format::kRGBA32Float) {
        const auto half_start = std::chrono::steady_clock::now();
        auto half_image = ConvertFloat32ToFloat16Image(cube.value());
        const auto half_end = std::chrono::steady_clock::now();
        AddDuration(decode_duration, half_start, half_end);
        if (!half_image.has_value()) {
          const auto error = half_image.error();
          session.AddDiagnostic({
            .severity = ImportSeverity::kError,
            .code = "texture.half_float_failed",
            .message
            = std::string("Half-float conversion failed: ") + to_string(error),
            .source_path = Request().source_path.string(),
            .object_path = {},
          });
          co_return StampDurations(source);
        }
        source.image = std::move(half_image.value());
      } else {
        source.image = std::move(cube.value());
      }
      source.meta = source.image->Meta();
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
      const auto source_id = Request().source_path.string();
      const auto decode_start = std::chrono::steady_clock::now();
      auto decoded = co_await ThreadPool()->Run(
        [data = bytes.data(), size = bytes.size(), options, source_id](
          oxygen::co::ThreadPool::CancelToken cancelled)
          -> oxygen::Result<ScratchImage, TextureImportError> {
          if (cancelled) {
            return ::oxygen::Err(TextureImportError::kCancelled);
          }
          auto result = DecodeToScratchImage(
            std::span<const std::byte>(data, size), options);
          return result;
        });
      const auto decode_end = std::chrono::steady_clock::now();
      AddDuration(decode_duration, decode_start, decode_end);
      if (!decoded.has_value()) {
        const auto error = decoded.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.decode_failed",
          .message = std::string("Decode failed: ") + to_string(error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      const auto extract_start = std::chrono::steady_clock::now();
      oxygen::Result<ScratchImage, TextureImportError> cube
        = co_await ExtractCubeFacesFromLayoutOnThreadPool(
          *ThreadPool(), decoded.value(), tuning.cubemap_layout);
      const auto extract_end = std::chrono::steady_clock::now();
      AddDuration(decode_duration, extract_start, extract_end);

      if (!cube.has_value()) {
        const auto error = cube.error();
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = "texture.cubemap_layout_failed",
          .message = std::string("Cubemap layout failed: ") + to_string(error),
          .source_path = Request().source_path.string(),
          .object_path = {},
        });
        co_return StampDurations(source);
      }

      if (WantsHalfFloatOutput(tuning)
        && cube->Meta().format == Format::kRGBA32Float) {
        const auto half_start = std::chrono::steady_clock::now();
        auto half_image = ConvertFloat32ToFloat16Image(cube.value());
        const auto half_end = std::chrono::steady_clock::now();
        AddDuration(decode_duration, half_start, half_end);
        if (!half_image.has_value()) {
          const auto error = half_image.error();
          session.AddDiagnostic({
            .severity = ImportSeverity::kError,
            .code = "texture.half_float_failed",
            .message
            = std::string("Half-float conversion failed: ") + to_string(error),
            .source_path = Request().source_path.string(),
            .object_path = {},
          });
          co_return StampDurations(source);
        }
        source.image = std::move(half_image.value());
      } else {
        source.image = std::move(cube.value());
      }
      source.meta = source.image->Meta();
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
    std::optional<ScratchImageMeta> meta;

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
      if (!meta.has_value()) {
        DecodeOptions face_options = options;
        face_options.extension_hint = face_path.extension().string();
        const auto source_id = face_path.string();
        const auto decode_start = std::chrono::steady_clock::now();
        auto decoded = co_await ThreadPool()->Run(
          [data = bytes.data(), size = bytes.size(), face_options, source_id](
            oxygen::co::ThreadPool::CancelToken cancelled)
            -> oxygen::Result<ScratchImage, TextureImportError> {
            if (cancelled) {
              return ::oxygen::Err(TextureImportError::kCancelled);
            }
            auto result = DecodeToScratchImage(
              std::span<const std::byte>(data, size), face_options);
            return result;
          });
        const auto decode_end = std::chrono::steady_clock::now();
        AddDuration(decode_duration, decode_start, decode_end);
        if (!decoded.has_value()) {
          const auto error = decoded.error();
          session.AddDiagnostic({
            .severity = ImportSeverity::kError,
            .code = "texture.decode_failed",
            .message = std::string("Decode failed: ") + to_string(error),
            .source_path = face_path.string(),
            .object_path = {},
          });
          co_return StampDurations(source);
        }

        const auto& face_meta = decoded->Meta();
        meta = ScratchImageMeta {
          .texture_type = TextureType::kTextureCube,
          .width = face_meta.width,
          .height = face_meta.height,
          .depth = 1,
          .array_layers = kCubeFaceCount,
          .mip_levels = 1,
          .format = face_meta.format,
        };
      }

      sources.AddCubeFace(
        static_cast<CubeFace>(i), std::move(bytes), face_path.string());
    }

    source.source_set = std::move(sources);
    source.meta = std::move(meta);
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

  const auto source_id = Request().source_path.string();
  const auto decode_start = std::chrono::steady_clock::now();
  auto decoded = co_await ThreadPool()->Run(
    [data = bytes.data(), size = bytes.size(), options, source_id](
      oxygen::co::ThreadPool::CancelToken cancelled)
      -> oxygen::Result<ScratchImage, TextureImportError> {
      if (cancelled) {
        return ::oxygen::Err(TextureImportError::kCancelled);
      }
      auto result
        = DecodeToScratchImage(std::span<const std::byte>(data, size), options);
      return result;
    });
  const auto decode_end = std::chrono::steady_clock::now();
  AddDuration(decode_duration, decode_start, decode_end);
  if (!decoded.has_value()) {
    const auto error = decoded.error();
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.decode_failed",
      .message = std::string("Decode failed: ") + to_string(error),
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return StampDurations(source);
  }

  if (WantsHalfFloatOutput(tuning)
    && decoded->Meta().format == Format::kRGBA32Float) {
    const auto half_start = std::chrono::steady_clock::now();
    auto half_image = ConvertFloat32ToFloat16Image(decoded.value());
    const auto half_end = std::chrono::steady_clock::now();
    AddDuration(decode_duration, half_start, half_end);
    if (!half_image.has_value()) {
      const auto error = half_image.error();
      session.AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "texture.half_float_failed",
        .message
        = std::string("Half-float conversion failed: ") + to_string(error),
        .source_path = Request().source_path.string(),
        .object_path = {},
      });
      co_return StampDurations(source);
    }
    source.image = std::move(half_image.value());
  } else {
    source.image = std::move(decoded.value());
  }
  source.meta = source.image->Meta();
  source.success = true;
  co_return StampDurations(source);
}

//! Cook the texture via the async TexturePipeline.
auto TextureImportJob::CookTexture(
  TextureSource& source, ImportSession& session, TexturePipeline& pipeline)
  -> co::Co<std::optional<CookedTexturePayload>>
{
  if (!source.meta.has_value()) {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.meta_missing",
      .message = "Texture source metadata is missing",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return std::nullopt;
  }

  const auto& meta = source.meta.value();
  const auto& tuning = Request().options.texture_tuning;

  TextureImportDesc desc {};
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
    desc.output_format = IsColorIntent(desc.intent) ? tuning.color_output_format
                                                    : tuning.data_output_format;
    desc.bc7_quality = tuning.bc7_quality;
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
      co_return std::nullopt;
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
  if (source.image.has_value()) {
    item.source = std::move(source.image.value());
  } else if (source.source_set.has_value()) {
    item.source = std::move(source.source_set.value());
  } else {
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.source_missing",
      .message = "Texture source data is missing",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return std::nullopt;
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
    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "texture.cook_failed",
      .message = "Texture pipeline did not return a cooked payload",
      .source_path = Request().source_path.string(),
      .object_path = {},
    });
    co_return std::nullopt;
  }

  co_return std::move(result.cooked.value());
}

//! Emit the cooked texture via TextureEmitter.
auto TextureImportJob::EmitTexture(
  CookedTexturePayload cooked, ImportSession& session) -> co::Co<bool>
{
  auto& emitter = session.TextureEmitter();
  const auto index = emitter.Emit(std::move(cooked));
  DLOG_F(INFO, "Texture emitted at index={}", index);
  co_return true;
}

//! Finalize the session and return the import report.
auto TextureImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
