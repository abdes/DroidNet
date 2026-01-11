//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/TextureImporter.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <optional>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/ImageDecode.h>
#include <Oxygen/Content/Import/TextureCooker.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h>

namespace oxygen::content::import {

namespace {

  //=== String Utilities ===--------------------------------------------------//

  [[nodiscard]] auto ToLower(std::string_view str) -> std::string
  {
    std::string result;
    result.reserve(str.size());
    for (const char ch : str) {
      result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
  }

  //! Check if string ends with suffix (case-insensitive).
  [[nodiscard]] auto EndsWithI(
    std::string_view str, std::string_view suffix) noexcept -> bool
  {
    if (suffix.size() > str.size()) {
      return false;
    }
    const auto start = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
      const auto ch_str = static_cast<char>(
        std::tolower(static_cast<unsigned char>(str[start + i])));
      const auto ch_suf = static_cast<char>(
        std::tolower(static_cast<unsigned char>(suffix[i])));
      if (ch_str != ch_suf) {
        return false;
      }
    }
    return true;
  }

  //! Check if stem ends with a suffix pattern (e.g., "_albedo").
  [[nodiscard]] auto StemEndsWithI(
    const std::filesystem::path& path, std::string_view suffix) noexcept -> bool
  {
    const auto stem = path.stem().string();
    return EndsWithI(stem, suffix);
  }

  //=== File I/O Utilities ===------------------------------------------------//

  [[nodiscard]] auto ReadFileBytes(const std::filesystem::path& path)
    -> oxygen::Result<std::vector<std::byte>, TextureImportError>
  {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      LOG_F(WARNING, "TextureImporter: file not found: {}", path.string());
      return ::oxygen::Err(TextureImportError::kFileNotFound);
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      LOG_F(WARNING, "TextureImporter: failed to open file: {}", path.string());
      return ::oxygen::Err(TextureImportError::kFileReadFailed);
    }

    const auto size = file.tellg();
    if (size <= 0) {
      LOG_F(WARNING, "TextureImporter: file is empty or unreadable: {}",
        path.string());
      return ::oxygen::Err(TextureImportError::kFileReadFailed);
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::byte> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()),
          static_cast<std::streamsize>(size))) {
      LOG_F(WARNING, "TextureImporter: failed to read file contents: {}",
        path.string());
      return ::oxygen::Err(TextureImportError::kFileReadFailed);
    }

    return ::oxygen::Ok(std::move(buffer));
  }

  //=== Validation Helpers ===------------------------------------------------//

  //! Validate that a span of image data is not empty.
  [[nodiscard]] auto ValidateInputData(std::span<const std::byte> data,
    std::string_view source_id) noexcept -> bool
  {
    if (data.empty()) {
      LOG_F(WARNING, "TextureImporter: empty input data for source: {}",
        std::string(source_id));
      return false;
    }
    return true;
  }

  //! Log a warning when preset was detected vs explicitly provided.
  void LogPresetSelection(const std::filesystem::path& path,
    TexturePreset detected, TexturePreset applied)
  {
    if (detected != applied) {
      DLOG_F(INFO,
        "TextureImporter: using explicit preset {} for '{}' "
        "(auto-detected would be {})",
        to_string(applied), path.filename().string(), to_string(detected));
    }
  }

  //! Log a warning when descriptor has unusual settings.
  void WarnOnUnusualDescriptor(const TextureImportDesc& desc)
  {
    // Warn if BC7 quality is set but output format is not BC7
    if (desc.bc7_quality != Bc7Quality::kNone
      && desc.output_format != Format::kBC7UNorm
      && desc.output_format != Format::kBC7UNormSRGB) {
      LOG_F(WARNING,
        "TextureImporter: bc7_quality is set to {} but output_format is not "
        "BC7 for source '{}'. BC7 compression will not be applied.",
        to_string(desc.bc7_quality), desc.source_id);
    }

    // Warn if flip_normal_green is set but intent is not normal map
    if (desc.flip_normal_green && desc.intent != TextureIntent::kNormalTS) {
      LOG_F(WARNING,
        "TextureImporter: flip_normal_green is true but intent is {} "
        "(not kNormalTS) for source '{}'. Setting will be ignored.",
        to_string(desc.intent), desc.source_id);
    }

    // Warn if renormalize is set but intent is not normal map
    if (desc.renormalize_normals_in_mips
      && desc.intent != TextureIntent::kNormalTS) {
      DLOG_F(INFO,
        "TextureImporter: renormalize_normals_in_mips is true but intent is {} "
        "for source '{}'. Setting may not have effect.",
        to_string(desc.intent), desc.source_id);
    }

    // Warn if HDR handling is kKeepFloat but output is already float
    if (desc.hdr_handling == HdrHandling::kKeepFloat) {
      const bool is_float_output = (desc.output_format == Format::kRGBA16Float
        || desc.output_format == Format::kRGBA32Float
        || desc.output_format == Format::kR16Float
        || desc.output_format == Format::kR32Float);
      if (!is_float_output) {
        DLOG_F(INFO,
          "TextureImporter: hdr_handling is kKeepFloat but output_format is {} "
          "for source '{}'. Output format may be overridden.",
          to_string(desc.output_format), desc.source_id);
      }
    }
  }

  //=== Cube Face Discovery ===-----------------------------------------------//

  //! Face suffix patterns to try for cube map discovery.
  struct CubeFaceSuffixSet {
    std::array<std::string_view, kCubeFaceCount> suffixes;
  };

  // clang-format off
  inline constexpr std::array<CubeFaceSuffixSet, 3> kCubeFaceSuffixSets = {{
    // Short form: px, nx, py, ny, pz, nz
    {{ "_px", "_nx", "_py", "_ny", "_pz", "_nz" }},
    // Long form: posx, negx, etc.
    {{ "_posx", "_negx", "_posy", "_negy", "_posz", "_negz" }},
    // Descriptive: right, left, top, bottom, front, back
    {{ "_right", "_left", "_top", "_bottom", "_front", "_back" }},
  }};
  // clang-format on

  //! Try to discover cube face files from a base path.
  /*!
    Attempts to find 6 face files using common naming conventions.

    @param base_path Base path without face suffix
    @return Array of 6 paths if all faces found, or nullopt
  */
  [[nodiscard]] auto DiscoverCubeFacePaths(
    const std::filesystem::path& base_path)
    -> std::optional<std::array<std::filesystem::path, kCubeFaceCount>>
  {
    const auto parent = base_path.parent_path();
    const auto stem = base_path.stem().string();
    const auto ext = base_path.extension().string();

    // Try each suffix set
    for (const auto& suffix_set : kCubeFaceSuffixSets) {
      std::array<std::filesystem::path, kCubeFaceCount> paths;
      bool all_found = true;

      for (size_t i = 0; i < kCubeFaceCount; ++i) {
        auto face_name = stem + std::string(suffix_set.suffixes[i]) + ext;
        auto face_path = parent / face_name;

        std::error_code ec;
        if (!std::filesystem::exists(face_path, ec)) {
          all_found = false;
          break;
        }
        paths[i] = std::move(face_path);
      }

      if (all_found) {
        return paths;
      }
    }

    return std::nullopt;
  }

} // namespace

//===----------------------------------------------------------------------===//
// Preset Auto-Detection
//===----------------------------------------------------------------------===//

auto DetectPresetFromFilename(const std::filesystem::path& filename) noexcept
  -> TexturePreset
{
  // Check extension first for HDR formats
  const auto ext = ToLower(filename.extension().string());
  if (ext == ".hdr" || ext == ".exr") {
    return TexturePreset::kHdrEnvironment;
  }

  // Check stem suffixes
  if (StemEndsWithI(filename, "_albedo")
    || StemEndsWithI(filename, "_basecolor")
    || StemEndsWithI(filename, "_diffuse")
    || StemEndsWithI(filename, "_color")) {
    return TexturePreset::kAlbedo;
  }

  if (StemEndsWithI(filename, "_normal") || StemEndsWithI(filename, "_nrm")) {
    return TexturePreset::kNormal;
  }

  if (StemEndsWithI(filename, "_roughness")
    || StemEndsWithI(filename, "_rough")) {
    return TexturePreset::kRoughness;
  }

  if (StemEndsWithI(filename, "_metallic")
    || StemEndsWithI(filename, "_metal")) {
    return TexturePreset::kMetallic;
  }

  if (StemEndsWithI(filename, "_ao") || StemEndsWithI(filename, "_occlusion")) {
    return TexturePreset::kAO;
  }

  if (StemEndsWithI(filename, "_orm")) {
    return TexturePreset::kORMPacked;
  }

  if (StemEndsWithI(filename, "_emissive")
    || StemEndsWithI(filename, "_emission")) {
    return TexturePreset::kEmissive;
  }

  if (StemEndsWithI(filename, "_height")
    || StemEndsWithI(filename, "_displacement")
    || StemEndsWithI(filename, "_disp") || StemEndsWithI(filename, "_bump")) {
    return TexturePreset::kHeightMap;
  }

  if (StemEndsWithI(filename, "_env") || StemEndsWithI(filename, "_hdri")) {
    return TexturePreset::kHdrEnvironment;
  }

  // Default to data texture
  return TexturePreset::kData;
}

//===----------------------------------------------------------------------===//
// ScratchImage Loading API
//===----------------------------------------------------------------------===//

namespace {

  //! Internal helper to load texture with flip_y option.
  [[nodiscard]] auto LoadTextureWithFlip(const std::filesystem::path& path,
    bool flip_y) -> oxygen::Result<ScratchImage, TextureImportError>
  {
    DCHECK_F(!path.empty(), "LoadTextureWithFlip: path must not be empty");

    auto bytes = ReadFileBytes(path);
    if (!bytes) {
      return ::oxygen::Err(bytes.error());
    }

    if (bytes->empty()) {
      LOG_F(
        WARNING, "TextureImporter: file contains no data: {}", path.string());
      return ::oxygen::Err(TextureImportError::kCorruptedData);
    }

    DecodeOptions options {
      .flip_y = flip_y,
      .force_rgba = true,
      .extension_hint = path.extension().string(),
    };

    auto result = DecodeToScratchImage(*bytes, options);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to decode image: {} (error: {})",
        path.string(), to_string(result.error()));
    }
    return result;
  }

} // namespace

auto LoadTexture(const std::filesystem::path& path)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  return LoadTextureWithFlip(path, false);
}

auto LoadTexture(
  const std::filesystem::path& path, const TextureImportDesc& desc)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  return LoadTextureWithFlip(path, desc.flip_y_on_decode);
}

auto LoadTexture(std::span<const std::byte> data, std::string_view source_id)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (!ValidateInputData(data, source_id)) {
    return ::oxygen::Err(TextureImportError::kCorruptedData);
  }

  DecodeOptions options {
    .flip_y = false,
    .force_rgba = true,
    .extension_hint = {},
  };

  // Extract extension hint from source_id
  if (const auto dot_pos = source_id.rfind('.');
    dot_pos != std::string_view::npos) {
    options.extension_hint = std::string(source_id.substr(dot_pos));
  }

  auto result = DecodeToScratchImage(data, options);
  if (!result) {
    LOG_F(WARNING,
      "TextureImporter: failed to decode image from memory: {} (error: {})",
      std::string(source_id), to_string(result.error()));
  }
  return result;
}

auto LoadTexture(std::span<const std::byte> data, const TextureImportDesc& desc)
  -> oxygen::Result<ScratchImage, TextureImportError>
{
  if (!ValidateInputData(data, desc.source_id)) {
    return ::oxygen::Err(TextureImportError::kCorruptedData);
  }

  DecodeOptions options {
    .flip_y = desc.flip_y_on_decode,
    .force_rgba = true,
    .extension_hint = {},
  };

  // Extract extension hint from source_id
  if (const auto dot_pos = desc.source_id.rfind('.');
    dot_pos != std::string::npos) {
    options.extension_hint = desc.source_id.substr(dot_pos);
  }

  auto result = DecodeToScratchImage(data, options);
  if (!result) {
    LOG_F(WARNING,
      "TextureImporter: failed to decode image from memory: {} (error: {})",
      desc.source_id, to_string(result.error()));
  }
  return result;
}

auto LoadTextures(std::span<const std::filesystem::path> paths)
  -> oxygen::Result<std::vector<ScratchImage>, TextureImportError>
{
  if (paths.empty()) {
    LOG_F(WARNING, "TextureImporter: LoadTextures called with empty paths");
    return ::oxygen::Err(TextureImportError::kFileNotFound);
  }

  std::vector<ScratchImage> images;
  images.reserve(paths.size());

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    auto result = LoadTexture(path);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load image {} of {}: {}",
        i + 1, paths.size(), path.string());
      return ::oxygen::Err(result.error());
    }
    images.push_back(std::move(*result));
  }

  return ::oxygen::Ok(std::move(images));
}

//===----------------------------------------------------------------------===//
// ScratchImage Cooking API
//===----------------------------------------------------------------------===//

auto CookScratchImage(ScratchImage&& image, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Validate input image
  if (!image.IsValid()) {
    LOG_F(
      WARNING, "TextureImporter: CookScratchImage called with invalid image");
    return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  DCHECK_F(image.Meta().width > 0 && image.Meta().height > 0,
    "CookScratchImage: image dimensions must be positive");

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.width = image.Meta().width;
  desc.height = image.Meta().height;
  desc.depth = image.Meta().depth;
  desc.array_layers = image.Meta().array_layers;
  desc.texture_type = image.Meta().texture_type;
  desc.source_id = "<memory>";

  // Cook the texture
  auto cooked = CookTexture(std::move(image), desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: cooking failed for preset {} (error: {})",
      to_string(preset), to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = desc.source_id,
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

auto CookScratchImage(ScratchImage&& image, const TextureImportDesc& desc,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Validate input image
  if (!image.IsValid()) {
    LOG_F(WARNING,
      "TextureImporter: CookScratchImage called with invalid image for '{}'",
      desc.source_id);
    return ::oxygen::Err(TextureImportError::kDecodeFailed);
  }

  // Warn about unusual descriptor settings
  WarnOnUnusualDescriptor(desc);

  // Create a working copy of the descriptor with image metadata
  TextureImportDesc resolved_desc = desc;
  if (resolved_desc.width == 0) {
    resolved_desc.width = image.Meta().width;
  }
  if (resolved_desc.height == 0) {
    resolved_desc.height = image.Meta().height;
  }
  if (resolved_desc.depth == 1 && image.Meta().depth > 1) {
    resolved_desc.depth = image.Meta().depth;
  }
  if (resolved_desc.array_layers == 1 && image.Meta().array_layers > 1) {
    resolved_desc.array_layers = image.Meta().array_layers;
  }

  // Validate the resolved descriptor
  if (auto error = resolved_desc.Validate()) {
    LOG_F(WARNING, "TextureImporter: descriptor validation failed for '{}': {}",
      desc.source_id, to_string(*error));
    return ::oxygen::Err(*error);
  }

  // Cook the texture
  auto cooked = CookTexture(std::move(image), resolved_desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: cooking failed for '{}' (error: {})",
      desc.source_id, to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = resolved_desc.source_id,
    .applied_preset = TexturePreset::kData,
  };

  return ::oxygen::Ok(std::move(result));
}

//===----------------------------------------------------------------------===//
// Single-File Import API
//===----------------------------------------------------------------------===//

auto ImportTexture(
  const std::filesystem::path& path, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  DCHECK_F(!path.empty(), "ImportTexture: path must not be empty");

  // Auto-detect preset from filename
  const TexturePreset preset = DetectPresetFromFilename(path);
  DLOG_F(INFO, "TextureImporter: auto-detected preset {} for '{}'",
    to_string(preset), path.filename().string());
  return ImportTexture(path, preset, policy);
}

auto ImportTexture(const std::filesystem::path& path, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  DCHECK_F(!path.empty(), "ImportTexture: path must not be empty");

  // Log preset selection for debugging
  const TexturePreset detected = DetectPresetFromFilename(path);
  LogPresetSelection(path, detected, preset);

  // Read file
  auto bytes = ReadFileBytes(path);
  if (!bytes) {
    return ::oxygen::Err(bytes.error());
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.source_id = path.string();

  // Cook the texture
  auto cooked = CookTexture(*bytes, desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: import failed for '{}' (error: {})",
      path.string(), to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = path.string(),
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

auto ImportTexture(const std::filesystem::path& path,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  DCHECK_F(!path.empty(), "ImportTexture: path must not be empty");

  // Read file
  auto bytes = ReadFileBytes(path);
  if (!bytes) {
    return ::oxygen::Err(bytes.error());
  }

  // Create a working copy of the descriptor
  TextureImportDesc resolved_desc = desc;
  if (resolved_desc.source_id.empty()) {
    resolved_desc.source_id = path.string();
  }

  // Warn about unusual settings
  WarnOnUnusualDescriptor(resolved_desc);

  // Cook the texture
  auto cooked = CookTexture(*bytes, resolved_desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: import failed for '{}' (error: {})",
      path.string(), to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = path.string(),
    .applied_preset = TexturePreset::kData, // Custom descriptor, no preset
  };

  return ::oxygen::Ok(std::move(result));
}

auto ImportTexture(std::span<const std::byte> data, std::string_view source_id,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (!ValidateInputData(data, source_id)) {
    return ::oxygen::Err(TextureImportError::kCorruptedData);
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.source_id = std::string(source_id);

  // Cook the texture
  auto cooked = CookTexture(data, desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: import failed for '{}' (error: {})",
      std::string(source_id), to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = std::string(source_id),
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

auto ImportTexture(std::span<const std::byte> data,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (!ValidateInputData(data, desc.source_id)) {
    return ::oxygen::Err(TextureImportError::kCorruptedData);
  }

  // Warn about unusual settings
  WarnOnUnusualDescriptor(desc);

  // Cook the texture
  auto cooked = CookTexture(data, desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: import failed for '{}' (error: {})",
      desc.source_id, to_string(cooked.error()));
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = desc.source_id,
    .applied_preset = TexturePreset::kData, // Custom descriptor, no preset
  };

  return ::oxygen::Ok(std::move(result));
}

//===----------------------------------------------------------------------===//
// Cube Map Import API
//===----------------------------------------------------------------------===//

namespace {

  //! Common implementation for cube map import from loaded faces.
  [[nodiscard]] auto ImportCubeMapFromFacesImpl(
    std::array<ScratchImage, kCubeFaceCount>&& faces,
    const TextureImportDesc& base_desc,
    const std::filesystem::path& first_face_path,
    const ITexturePackingPolicy& policy)
    -> oxygen::Result<TextureImportResult, TextureImportError>
  {
    // Validate all faces have matching dimensions
    const auto& first_meta = faces[0].Meta();
    for (size_t i = 1; i < kCubeFaceCount; ++i) {
      const auto& meta = faces[i].Meta();
      if (meta.width != first_meta.width || meta.height != first_meta.height) {
        LOG_F(WARNING,
          "TextureImporter: cube face {} has different dimensions "
          "({}x{}) vs face 0 ({}x{})",
          i, meta.width, meta.height, first_meta.width, first_meta.height);
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
      if (meta.format != first_meta.format) {
        LOG_F(WARNING,
          "TextureImporter: cube face {} has different format ({}) vs face 0 "
          "({})",
          i, to_string(meta.format), to_string(first_meta.format));
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
    }

    // Assemble into cube map
    auto cube = AssembleCubeFromFaces(
      std::span<const ScratchImage, kCubeFaceCount>(faces));
    if (!cube) {
      LOG_F(WARNING, "TextureImporter: failed to assemble cube map");
      return ::oxygen::Err(cube.error());
    }

    // Create resolved descriptor
    TextureImportDesc desc = base_desc;
    desc.texture_type = TextureType::kTextureCube;
    desc.width = cube->Meta().width;
    desc.height = cube->Meta().height;
    desc.array_layers = kCubeFaceCount;
    if (desc.source_id.empty()) {
      desc.source_id = first_face_path.string();
    }

    // Warn about unusual settings
    WarnOnUnusualDescriptor(desc);

    // Cook the texture
    auto cooked = CookTexture(std::move(*cube), desc, policy);
    if (!cooked) {
      LOG_F(WARNING, "TextureImporter: cube map cooking failed (error: {})",
        to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    TextureImportResult result {
      .payload = std::move(*cooked),
      .source_path = first_face_path.string(),
      .applied_preset = TexturePreset::kData,
    };

    return ::oxygen::Ok(std::move(result));
  }

} // namespace

auto ImportCubeMap(
  std::span<const std::filesystem::path, kCubeFaceCount> face_paths,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Load all face images
  std::array<ScratchImage, kCubeFaceCount> faces;
  for (size_t i = 0; i < kCubeFaceCount; ++i) {
    auto result = LoadTexture(face_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load cube face {}: {}", i,
        face_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    faces[i] = std::move(*result);
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);

  auto import_result
    = ImportCubeMapFromFacesImpl(std::move(faces), desc, face_paths[0], policy);
  if (import_result) {
    import_result->applied_preset = preset;
  }
  return import_result;
}

auto ImportCubeMap(
  std::span<const std::filesystem::path, kCubeFaceCount> face_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Load all face images
  std::array<ScratchImage, kCubeFaceCount> faces;
  for (size_t i = 0; i < kCubeFaceCount; ++i) {
    auto result = LoadTexture(face_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load cube face {}: {}", i,
        face_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    faces[i] = std::move(*result);
  }

  return ImportCubeMapFromFacesImpl(
    std::move(faces), desc, face_paths[0], policy);
}

auto ImportCubeMap(const std::filesystem::path& base_path, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  DCHECK_F(!base_path.empty(), "ImportCubeMap: base_path must not be empty");

  // Discover face paths
  auto discovered = DiscoverCubeFacePaths(base_path);
  if (!discovered) {
    LOG_F(WARNING,
      "TextureImporter: could not discover cube face files for base path: {}",
      base_path.string());
    return ::oxygen::Err(TextureImportError::kFileNotFound);
  }

  return ImportCubeMap(
    std::span<const std::filesystem::path, kCubeFaceCount>(*discovered), preset,
    policy);
}

auto ImportCubeMapFromEquirect(const std::filesystem::path& equirect_path,
  uint32_t face_size, TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Load equirectangular panorama
  auto equirect = LoadTexture(equirect_path);
  if (!equirect) {
    return ::oxygen::Err(equirect.error());
  }

  // For HDR conversion, we need the image in float format
  // If it's already float, use it directly; otherwise, conversion is needed
  ScratchImage float_image;
  if (equirect->Meta().format == Format::kRGBA32Float) {
    float_image = std::move(*equirect);
  } else {
    // Need to convert RGBA8 to float for proper sampling
    // Create float version
    const auto& meta = equirect->Meta();
    auto float_scratch = ScratchImage::Create(ScratchImageMeta {
      .texture_type = TextureType::kTexture2D,
      .width = meta.width,
      .height = meta.height,
      .depth = 1,
      .array_layers = 1,
      .mip_levels = 1,
      .format = Format::kRGBA32Float,
    });

    if (!float_scratch.IsValid()) {
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    // Convert pixels
    const auto src_view = equirect->GetImage(0, 0);
    auto dst_pixels = float_scratch.GetMutablePixels(0, 0);
    const auto* src_ptr = src_view.pixels.data();
    auto* dst_ptr = reinterpret_cast<float*>(dst_pixels.data());

    const size_t pixel_count = meta.width * meta.height;
    for (size_t i = 0; i < pixel_count; ++i) {
      for (size_t c = 0; c < 4; ++c) {
        const uint8_t byte_val = static_cast<uint8_t>(src_ptr[i * 4 + c]);
        dst_ptr[i * 4 + c] = static_cast<float>(byte_val) / 255.0F;
      }
    }

    float_image = std::move(float_scratch);
  }

  // Convert to cube map
  EquirectToCubeOptions options {
    .face_size = face_size,
    .sample_filter = MipFilter::kKaiser,
  };

  auto cube = ConvertEquirectangularToCube(float_image, options);
  if (!cube) {
    return ::oxygen::Err(cube.error());
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.texture_type = TextureType::kTextureCube;
  desc.width = face_size;
  desc.height = face_size;
  desc.array_layers = kCubeFaceCount;
  desc.source_id = equirect_path.string();

  // Cook the texture
  auto cooked = CookTexture(std::move(*cube), desc, policy);
  if (!cooked) {
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = equirect_path.string(),
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

//===----------------------------------------------------------------------===//
// Cube Map From Layout Image Import API
//===----------------------------------------------------------------------===//

auto ImportCubeMapFromLayoutImage(const std::filesystem::path& path,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Load the layout image using descriptor (respects flip_y_on_decode)
  auto layout_image = LoadTexture(path, desc);
  if (!layout_image) {
    LOG_F(WARNING, "TextureImporter: failed to load layout image: {}",
      path.string());
    return ::oxygen::Err(layout_image.error());
  }

  // Detect layout
  const auto detection = DetectCubeMapLayout(*layout_image);
  if (!detection.has_value()) {
    LOG_F(WARNING,
      "TextureImporter: cannot detect cube map layout from image dimensions "
      "({}x{}): {}",
      layout_image->Meta().width, layout_image->Meta().height, path.string());
    return ::oxygen::Err(TextureImportError::kDimensionMismatch);
  }

  LOG_F(INFO, "TextureImporter: detected {} layout with {}px faces: {}",
    to_string(detection->layout), detection->face_size, path.string());

  // Extract faces from layout
  auto cube = ExtractCubeFacesFromLayout(*layout_image, detection->layout);
  if (!cube) {
    LOG_F(WARNING,
      "TextureImporter: failed to extract cube faces from layout: {}",
      path.string());
    return ::oxygen::Err(cube.error());
  }

  // Create resolved descriptor
  TextureImportDesc resolved_desc = desc;
  resolved_desc.texture_type = TextureType::kTextureCube;
  resolved_desc.width = detection->face_size;
  resolved_desc.height = detection->face_size;
  resolved_desc.array_layers = kCubeFaceCount;
  if (resolved_desc.source_id.empty()) {
    resolved_desc.source_id = path.string();
  }

  // Cook the texture
  auto cooked = CookTexture(std::move(*cube), resolved_desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: cube map cooking failed (error: {}): {}",
      to_string(cooked.error()), path.string());
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = path.string(),
    .applied_preset = TexturePreset::kData,
  };

  return ::oxygen::Ok(std::move(result));
}

auto ImportCubeMapFromLayoutImage(const std::filesystem::path& path,
  const CubeMapImageLayout layout, const TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (layout == CubeMapImageLayout::kUnknown) {
    LOG_F(WARNING, "TextureImporter: explicit layout cannot be kUnknown: {}",
      path.string());
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Load the layout image
  auto layout_image = LoadTexture(path);
  if (!layout_image) {
    LOG_F(WARNING, "TextureImporter: failed to load layout image: {}",
      path.string());
    return ::oxygen::Err(layout_image.error());
  }

  // Verify layout is compatible with image dimensions
  const auto detection = DetectCubeMapLayout(*layout_image);
  if (!detection.has_value()) {
    LOG_F(WARNING,
      "TextureImporter: image dimensions ({}x{}) don't match any cube map "
      "layout: {}",
      layout_image->Meta().width, layout_image->Meta().height, path.string());
    return ::oxygen::Err(TextureImportError::kDimensionMismatch);
  }

  if (detection->layout != layout) {
    LOG_F(WARNING,
      "TextureImporter: explicit layout {} doesn't match detected layout {} "
      "for image ({}x{}): {}",
      to_string(layout), to_string(detection->layout),
      layout_image->Meta().width, layout_image->Meta().height, path.string());
    return ::oxygen::Err(TextureImportError::kDimensionMismatch);
  }

  LOG_F(INFO, "TextureImporter: using {} layout with {}px faces: {}",
    to_string(layout), detection->face_size, path.string());

  // Extract faces from layout
  auto cube = ExtractCubeFacesFromLayout(*layout_image, layout);
  if (!cube) {
    LOG_F(WARNING,
      "TextureImporter: failed to extract cube faces from layout: {}",
      path.string());
    return ::oxygen::Err(cube.error());
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.texture_type = TextureType::kTextureCube;
  desc.width = detection->face_size;
  desc.height = detection->face_size;
  desc.array_layers = kCubeFaceCount;
  desc.source_id = path.string();

  // Cook the texture
  auto cooked = CookTexture(std::move(*cube), desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: cube map cooking failed (error: {}): {}",
      to_string(cooked.error()), path.string());
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = path.string(),
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

auto ImportCubeMapFromLayoutImage(const std::filesystem::path& path,
  const TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Load the layout image
  auto layout_image = LoadTexture(path);
  if (!layout_image) {
    LOG_F(WARNING, "TextureImporter: failed to load layout image: {}",
      path.string());
    return ::oxygen::Err(layout_image.error());
  }

  // Detect layout
  const auto detection = DetectCubeMapLayout(*layout_image);
  if (!detection.has_value()) {
    LOG_F(WARNING,
      "TextureImporter: cannot detect cube map layout from image dimensions "
      "({}x{}): {}",
      layout_image->Meta().width, layout_image->Meta().height, path.string());
    return ::oxygen::Err(TextureImportError::kDimensionMismatch);
  }

  LOG_F(INFO, "TextureImporter: detected {} layout with {}px faces: {}",
    to_string(detection->layout), detection->face_size, path.string());

  // Extract faces from layout
  auto cube = ExtractCubeFacesFromLayout(*layout_image, detection->layout);
  if (!cube) {
    LOG_F(WARNING,
      "TextureImporter: failed to extract cube faces from layout: {}",
      path.string());
    return ::oxygen::Err(cube.error());
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);
  desc.texture_type = TextureType::kTextureCube;
  desc.width = detection->face_size;
  desc.height = detection->face_size;
  desc.array_layers = kCubeFaceCount;
  desc.source_id = path.string();

  // Cook the texture
  auto cooked = CookTexture(std::move(*cube), desc, policy);
  if (!cooked) {
    LOG_F(WARNING, "TextureImporter: cube map cooking failed (error: {}): {}",
      to_string(cooked.error()), path.string());
    return ::oxygen::Err(cooked.error());
  }

  TextureImportResult result {
    .payload = std::move(*cooked),
    .source_path = path.string(),
    .applied_preset = preset,
  };

  return ::oxygen::Ok(std::move(result));
}

//===----------------------------------------------------------------------===//
// Texture Array Import API
//===----------------------------------------------------------------------===//

namespace {

  //! Common implementation for texture array import from loaded layers.
  [[nodiscard]] auto ImportTextureArrayImpl(std::vector<ScratchImage>&& layers,
    const TextureImportDesc& base_desc,
    const std::filesystem::path& first_layer_path,
    const ITexturePackingPolicy& policy)
    -> oxygen::Result<TextureImportResult, TextureImportError>
  {
    DCHECK_F(
      !layers.empty(), "ImportTextureArrayImpl: layers must not be empty");

    // Validate dimensions match
    const auto& first_meta = layers[0].Meta();
    for (size_t i = 1; i < layers.size(); ++i) {
      const auto& meta = layers[i].Meta();
      if (meta.width != first_meta.width || meta.height != first_meta.height) {
        LOG_F(WARNING,
          "TextureImporter: array layer {} has different dimensions "
          "({}x{}) vs layer 0 ({}x{})",
          i, meta.width, meta.height, first_meta.width, first_meta.height);
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
      if (meta.format != first_meta.format) {
        LOG_F(WARNING,
          "TextureImporter: array layer {} has different format ({}) vs layer "
          "0 ({})",
          i, to_string(meta.format), to_string(first_meta.format));
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
    }

    // Create array texture metadata
    const auto array_layers = static_cast<uint16_t>(layers.size());
    ScratchImageMeta array_meta {
      .texture_type = TextureType::kTexture2DArray,
      .width = first_meta.width,
      .height = first_meta.height,
      .depth = 1,
      .array_layers = array_layers,
      .mip_levels = 1,
      .format = first_meta.format,
    };

    auto array_image = ScratchImage::Create(array_meta);
    if (!array_image.IsValid()) {
      LOG_F(WARNING, "TextureImporter: failed to allocate array texture");
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    // Copy each layer into the array
    for (size_t i = 0; i < layers.size(); ++i) {
      const auto src_view = layers[i].GetImage(0, 0);
      auto dst_pixels
        = array_image.GetMutablePixels(static_cast<uint16_t>(i), 0);

      if (src_view.pixels.size() != dst_pixels.size()) {
        LOG_F(WARNING,
          "TextureImporter: pixel size mismatch for array layer {}", i);
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }

      std::copy(
        src_view.pixels.begin(), src_view.pixels.end(), dst_pixels.data());
    }

    // Create resolved descriptor
    TextureImportDesc desc = base_desc;
    desc.texture_type = TextureType::kTexture2DArray;
    desc.width = first_meta.width;
    desc.height = first_meta.height;
    desc.array_layers = array_layers;
    if (desc.source_id.empty()) {
      desc.source_id = first_layer_path.string();
    }

    // Warn about unusual settings
    WarnOnUnusualDescriptor(desc);

    // Cook the texture
    auto cooked = CookTexture(std::move(array_image), desc, policy);
    if (!cooked) {
      LOG_F(WARNING,
        "TextureImporter: texture array cooking failed (error: {})",
        to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    TextureImportResult result {
      .payload = std::move(*cooked),
      .source_path = first_layer_path.string(),
      .applied_preset = TexturePreset::kData,
    };

    return ::oxygen::Ok(std::move(result));
  }

} // namespace

auto ImportTextureArray(std::span<const std::filesystem::path> layer_paths,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (layer_paths.empty()) {
    LOG_F(
      WARNING, "TextureImporter: ImportTextureArray called with empty paths");
    return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
  }

  // Load all layers
  std::vector<ScratchImage> layers;
  layers.reserve(layer_paths.size());

  for (size_t i = 0; i < layer_paths.size(); ++i) {
    auto result = LoadTexture(layer_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load array layer {}: {}", i,
        layer_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    layers.push_back(std::move(*result));
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);

  auto import_result
    = ImportTextureArrayImpl(std::move(layers), desc, layer_paths[0], policy);
  if (import_result) {
    import_result->applied_preset = preset;
  }
  return import_result;
}

auto ImportTextureArray(std::span<const std::filesystem::path> layer_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (layer_paths.empty()) {
    LOG_F(
      WARNING, "TextureImporter: ImportTextureArray called with empty paths");
    return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
  }

  // Load all layers
  std::vector<ScratchImage> layers;
  layers.reserve(layer_paths.size());

  for (size_t i = 0; i < layer_paths.size(); ++i) {
    auto result = LoadTexture(layer_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load array layer {}: {}", i,
        layer_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    layers.push_back(std::move(*result));
  }

  return ImportTextureArrayImpl(
    std::move(layers), desc, layer_paths[0], policy);
}

//===----------------------------------------------------------------------===//
// 3D Texture Import API
//===----------------------------------------------------------------------===//

namespace {

  //! Common implementation for 3D texture import from loaded slices.
  [[nodiscard]] auto ImportTexture3DImpl(std::vector<ScratchImage>&& slices,
    const TextureImportDesc& base_desc,
    const std::filesystem::path& first_slice_path,
    const ITexturePackingPolicy& policy)
    -> oxygen::Result<TextureImportResult, TextureImportError>
  {
    DCHECK_F(!slices.empty(), "ImportTexture3DImpl: slices must not be empty");

    // Validate dimensions match
    const auto& first_meta = slices[0].Meta();
    for (size_t i = 1; i < slices.size(); ++i) {
      const auto& meta = slices[i].Meta();
      if (meta.width != first_meta.width || meta.height != first_meta.height) {
        LOG_F(WARNING,
          "TextureImporter: 3D slice {} has different dimensions "
          "({}x{}) vs slice 0 ({}x{})",
          i, meta.width, meta.height, first_meta.width, first_meta.height);
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
      if (meta.format != first_meta.format) {
        LOG_F(WARNING,
          "TextureImporter: 3D slice {} has different format ({}) vs slice 0 "
          "({})",
          i, to_string(meta.format), to_string(first_meta.format));
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }
    }

    // Create 3D texture metadata
    const auto depth = static_cast<uint16_t>(slices.size());
    ScratchImageMeta volume_meta {
      .texture_type = TextureType::kTexture3D,
      .width = first_meta.width,
      .height = first_meta.height,
      .depth = depth,
      .array_layers = 1,
      .mip_levels = 1,
      .format = first_meta.format,
    };

    auto volume_image = ScratchImage::Create(volume_meta);
    if (!volume_image.IsValid()) {
      LOG_F(WARNING, "TextureImporter: failed to allocate 3D texture");
      return ::oxygen::Err(TextureImportError::kOutOfMemory);
    }

    // For 3D textures, all slices are stored in layer 0, mip 0
    // The storage is contiguous: slice 0, slice 1, ..., slice N-1
    auto dst_pixels = volume_image.GetMutablePixels(0, 0);
    const size_t slice_size = first_meta.width * first_meta.height
      * (first_meta.format == Format::kRGBA32Float ? 16 : 4);

    for (size_t i = 0; i < slices.size(); ++i) {
      const auto src_view = slices[i].GetImage(0, 0);
      if (src_view.pixels.size() != slice_size) {
        LOG_F(
          WARNING, "TextureImporter: pixel size mismatch for 3D slice {}", i);
        return ::oxygen::Err(TextureImportError::kDimensionMismatch);
      }

      std::copy(src_view.pixels.begin(), src_view.pixels.end(),
        dst_pixels.data() + i * slice_size);
    }

    // Create resolved descriptor
    TextureImportDesc desc = base_desc;
    desc.texture_type = TextureType::kTexture3D;
    desc.width = first_meta.width;
    desc.height = first_meta.height;
    desc.depth = depth;
    if (desc.source_id.empty()) {
      desc.source_id = first_slice_path.string();
    }

    // Warn about unusual settings
    WarnOnUnusualDescriptor(desc);

    // Cook the texture
    auto cooked = CookTexture(std::move(volume_image), desc, policy);
    if (!cooked) {
      LOG_F(WARNING, "TextureImporter: 3D texture cooking failed (error: {})",
        to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    TextureImportResult result {
      .payload = std::move(*cooked),
      .source_path = first_slice_path.string(),
      .applied_preset = TexturePreset::kData,
    };

    return ::oxygen::Ok(std::move(result));
  }

} // namespace

auto ImportTexture3D(std::span<const std::filesystem::path> slice_paths,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (slice_paths.empty()) {
    LOG_F(WARNING, "TextureImporter: ImportTexture3D called with empty paths");
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Load all slices
  std::vector<ScratchImage> slices;
  slices.reserve(slice_paths.size());

  for (size_t i = 0; i < slice_paths.size(); ++i) {
    auto result = LoadTexture(slice_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load 3D slice {}: {}", i,
        slice_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    slices.push_back(std::move(*result));
  }

  // Create descriptor from preset
  TextureImportDesc desc = MakeDescFromPreset(preset);

  auto import_result
    = ImportTexture3DImpl(std::move(slices), desc, slice_paths[0], policy);
  if (import_result) {
    import_result->applied_preset = preset;
  }
  return import_result;
}

auto ImportTexture3D(std::span<const std::filesystem::path> slice_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  if (slice_paths.empty()) {
    LOG_F(WARNING, "TextureImporter: ImportTexture3D called with empty paths");
    return ::oxygen::Err(TextureImportError::kInvalidDimensions);
  }

  // Load all slices
  std::vector<ScratchImage> slices;
  slices.reserve(slice_paths.size());

  for (size_t i = 0; i < slice_paths.size(); ++i) {
    auto result = LoadTexture(slice_paths[i]);
    if (!result) {
      LOG_F(WARNING, "TextureImporter: failed to load 3D slice {}: {}", i,
        slice_paths[i].string());
      return ::oxygen::Err(result.error());
    }
    slices.push_back(std::move(*result));
  }

  return ImportTexture3DImpl(std::move(slices), desc, slice_paths[0], policy);
}

//===----------------------------------------------------------------------===//
// TextureImportBuilder Implementation
//===----------------------------------------------------------------------===//

//! Internal implementation of TextureImportBuilder.
struct TextureImportBuilder::Impl {
  //=== Source Data ===-------------------------------------------------------//

  //! Single file source path (for simple 2D textures).
  std::optional<std::filesystem::path> source_path;

  //! Single memory source (for in-memory data).
  std::optional<std::pair<std::vector<std::byte>, std::string>> source_memory;

  //! Cube face sources (for cube map assembly).
  std::array<std::optional<std::filesystem::path>, kCubeFaceCount> cube_faces;

  //! Array layer sources (for texture array assembly).
  std::vector<std::pair<uint16_t, std::filesystem::path>> array_layers;

  //! Depth slice sources (for 3D texture assembly).
  std::vector<std::pair<uint16_t, std::filesystem::path>> depth_slices;

  //=== Configuration ===-----------------------------------------------------//

  //! Applied preset (if any).
  std::optional<TexturePreset> preset;

  //! Custom descriptor (alternative to preset).
  std::optional<TextureImportDesc> custom_desc;

  //! Explicit texture type override.
  std::optional<TextureType> texture_type;

  //! Explicit output format override.
  std::optional<Format> output_format;

  //! Explicit source color space override.
  std::optional<ColorSpace> source_color_space;

  //=== Mip Configuration ===------------------------------------------------//

  //! Mip generation policy override.
  std::optional<MipPolicy> mip_policy;

  //! Maximum mip levels override.
  std::optional<uint8_t> max_mip_levels;

  //! Mip filter override.
  std::optional<MipFilter> mip_filter;

  //=== Content-Specific Options ===------------------------------------------//

  //! Flip normal green channel.
  std::optional<bool> flip_normal_green;

  //! Renormalize normals in mips.
  std::optional<bool> renormalize_normals_in_mips;

  //! Flip Y on decode.
  std::optional<bool> flip_y_on_decode;

  //=== Compression ===-------------------------------------------------------//

  //! BC7 quality override.
  std::optional<Bc7Quality> bc7_quality;

  //=== HDR ===---------------------------------------------------------------//

  //! HDR handling policy override.
  std::optional<HdrHandling> hdr_handling;

  //! Exposure adjustment override.
  std::optional<float> exposure_ev;

  //=== Helper Methods ===----------------------------------------------------//

  [[nodiscard]] auto InferTextureType() const -> TextureType
  {
    // Check for explicit override
    if (texture_type) {
      return *texture_type;
    }

    // Check if custom descriptor has a texture type set
    if (custom_desc && custom_desc->texture_type != TextureType::kTexture2D) {
      return custom_desc->texture_type;
    }

    // Check for cube map sources
    bool has_any_cube_face = false;
    for (const auto& face : cube_faces) {
      if (face) {
        has_any_cube_face = true;
        break;
      }
    }
    if (has_any_cube_face) {
      return TextureType::kTextureCube;
    }

    // Check for array layers
    if (!array_layers.empty()) {
      return TextureType::kTexture2DArray;
    }

    // Check for depth slices
    if (!depth_slices.empty()) {
      return TextureType::kTexture3D;
    }

    // Default to 2D
    return TextureType::kTexture2D;
  }

  void ApplyOverrides(TextureImportDesc& desc) const
  {
    if (texture_type) {
      desc.texture_type = *texture_type;
    }
    if (output_format) {
      desc.output_format = *output_format;
    }
    if (source_color_space) {
      desc.source_color_space = *source_color_space;
    }
    if (mip_policy) {
      desc.mip_policy = *mip_policy;
    }
    if (max_mip_levels) {
      desc.max_mip_levels = *max_mip_levels;
    }
    if (mip_filter) {
      desc.mip_filter = *mip_filter;
    }
    if (flip_normal_green) {
      desc.flip_normal_green = *flip_normal_green;
    }
    if (renormalize_normals_in_mips) {
      desc.renormalize_normals_in_mips = *renormalize_normals_in_mips;
    }
    if (flip_y_on_decode) {
      desc.flip_y_on_decode = *flip_y_on_decode;
    }
    if (bc7_quality) {
      desc.bc7_quality = *bc7_quality;
    }
    if (hdr_handling) {
      desc.hdr_handling = *hdr_handling;
    }
    if (exposure_ev) {
      desc.exposure_ev = *exposure_ev;
    }
  }
};

TextureImportBuilder::TextureImportBuilder()
  : impl_(std::make_unique<Impl>())
{
}

TextureImportBuilder::TextureImportBuilder(
  TextureImportBuilder&& other) noexcept
  = default;

auto TextureImportBuilder::operator=(TextureImportBuilder&& other) noexcept
  -> TextureImportBuilder& = default;

TextureImportBuilder::~TextureImportBuilder() = default;

//=== Source Configuration ===------------------------------------------------//

auto TextureImportBuilder::FromFile(std::filesystem::path path)
  -> TextureImportBuilder&
{
  impl_->source_path = std::move(path);
  return *this;
}

auto TextureImportBuilder::FromMemory(
  std::vector<std::byte> data, std::string source_id) -> TextureImportBuilder&
{
  impl_->source_memory = std::make_pair(std::move(data), std::move(source_id));
  return *this;
}

auto TextureImportBuilder::AddCubeFace(
  CubeFace face, std::filesystem::path path) -> TextureImportBuilder&
{
  impl_->cube_faces[static_cast<size_t>(face)] = std::move(path);
  return *this;
}

auto TextureImportBuilder::AddArrayLayer(
  uint16_t layer, std::filesystem::path path) -> TextureImportBuilder&
{
  impl_->array_layers.emplace_back(layer, std::move(path));
  return *this;
}

auto TextureImportBuilder::AddDepthSlice(
  uint16_t slice, std::filesystem::path path) -> TextureImportBuilder&
{
  impl_->depth_slices.emplace_back(slice, std::move(path));
  return *this;
}

//=== Preset & Format Configuration ===---------------------------------------//

auto TextureImportBuilder::WithPreset(TexturePreset preset)
  -> TextureImportBuilder&
{
  impl_->preset = preset;
  impl_->custom_desc
    = std::nullopt; // Clear custom descriptor when preset is set
  return *this;
}

auto TextureImportBuilder::WithDescriptor(const TextureImportDesc& desc)
  -> TextureImportBuilder&
{
  impl_->custom_desc = desc;
  impl_->preset = std::nullopt; // Clear preset when custom descriptor is set
  return *this;
}

auto TextureImportBuilder::WithTextureType(TextureType type)
  -> TextureImportBuilder&
{
  impl_->texture_type = type;
  return *this;
}

auto TextureImportBuilder::WithOutputFormat(Format format)
  -> TextureImportBuilder&
{
  impl_->output_format = format;
  return *this;
}

auto TextureImportBuilder::WithSourceColorSpace(ColorSpace space)
  -> TextureImportBuilder&
{
  impl_->source_color_space = space;
  return *this;
}

//=== Mip Configuration ===---------------------------------------------------//

auto TextureImportBuilder::WithMipPolicy(MipPolicy policy)
  -> TextureImportBuilder&
{
  impl_->mip_policy = policy;
  return *this;
}

auto TextureImportBuilder::WithMaxMipLevels(uint8_t levels)
  -> TextureImportBuilder&
{
  impl_->max_mip_levels = levels;
  return *this;
}

auto TextureImportBuilder::WithMipFilter(MipFilter filter)
  -> TextureImportBuilder&
{
  impl_->mip_filter = filter;
  return *this;
}

//=== Content-Specific Options ===--------------------------------------------//

auto TextureImportBuilder::FlipNormalGreen(bool flip) -> TextureImportBuilder&
{
  impl_->flip_normal_green = flip;
  return *this;
}

auto TextureImportBuilder::RenormalizeNormalsInMips(bool renormalize)
  -> TextureImportBuilder&
{
  impl_->renormalize_normals_in_mips = renormalize;
  return *this;
}

auto TextureImportBuilder::FlipYOnDecode(bool flip) -> TextureImportBuilder&
{
  impl_->flip_y_on_decode = flip;
  return *this;
}

//=== Compression Options ===------------------------------------------------//

auto TextureImportBuilder::WithBc7Quality(Bc7Quality quality)
  -> TextureImportBuilder&
{
  impl_->bc7_quality = quality;
  return *this;
}

//=== HDR Options ===--------------------------------------------------------//

auto TextureImportBuilder::WithHdrHandling(HdrHandling handling)
  -> TextureImportBuilder&
{
  impl_->hdr_handling = handling;
  return *this;
}

auto TextureImportBuilder::WithExposure(float ev) -> TextureImportBuilder&
{
  impl_->exposure_ev = ev;
  return *this;
}

//=== Build ===--------------------------------------------------------------//

auto TextureImportBuilder::Build(const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>
{
  // Determine the texture type
  const TextureType inferred_type = impl_->InferTextureType();

  // Build descriptor from custom descriptor, preset, or defaults
  TextureImportDesc desc;
  if (impl_->custom_desc) {
    desc = *impl_->custom_desc;
    DLOG_F(INFO, "TextureImportBuilder: using custom descriptor");
  } else if (impl_->preset) {
    desc = MakeDescFromPreset(*impl_->preset);
    DLOG_F(
      INFO, "TextureImportBuilder: using preset {}", to_string(*impl_->preset));
  } else {
    // Default to kData preset
    desc = MakeDescFromPreset(TexturePreset::kData);
    DLOG_F(INFO,
      "TextureImportBuilder: no preset or descriptor specified, using kData");
  }

  // Apply overrides
  impl_->ApplyOverrides(desc);
  desc.texture_type = inferred_type;

  // Warn about unusual settings
  WarnOnUnusualDescriptor(desc);

  // Handle different source types
  if (inferred_type == TextureType::kTextureCube) {
    // Cube map from faces
    bool all_faces_present = true;
    size_t missing_face = 0;
    for (size_t i = 0; i < kCubeFaceCount; ++i) {
      if (!impl_->cube_faces[i]) {
        all_faces_present = false;
        missing_face = i;
        break;
      }
    }

    if (!all_faces_present) {
      LOG_F(WARNING,
        "TextureImportBuilder: cube map missing face {} (expected 6 faces)",
        missing_face);
      return ::oxygen::Err(TextureImportError::kArrayLayerCountInvalid);
    }

    // Load all faces
    std::array<ScratchImage, kCubeFaceCount> faces;
    for (size_t i = 0; i < kCubeFaceCount; ++i) {
      auto result = LoadTexture(*impl_->cube_faces[i]);
      if (!result) {
        LOG_F(WARNING, "TextureImportBuilder: failed to load cube face {}: {}",
          i, impl_->cube_faces[i]->string());
        return ::oxygen::Err(result.error());
      }
      faces[i] = std::move(*result);
    }

    // Assemble
    auto cube = AssembleCubeFromFaces(
      std::span<const ScratchImage, kCubeFaceCount>(faces));
    if (!cube) {
      LOG_F(WARNING, "TextureImportBuilder: failed to assemble cube map");
      return ::oxygen::Err(cube.error());
    }

    desc.width = cube->Meta().width;
    desc.height = cube->Meta().height;
    desc.array_layers = kCubeFaceCount;
    desc.source_id = impl_->cube_faces[0]->string();

    auto cooked = CookTexture(std::move(*cube), desc, policy);
    if (!cooked) {
      LOG_F(WARNING, "TextureImportBuilder: cube map cooking failed: {}",
        to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    return ::oxygen::Ok(TextureImportResult {
      .payload = std::move(*cooked),
      .source_path = desc.source_id,
      .applied_preset = impl_->preset.value_or(TexturePreset::kData),
    });
  }

  if (inferred_type == TextureType::kTexture2DArray
    && !impl_->array_layers.empty()) {
    // Sort layers by index
    std::sort(impl_->array_layers.begin(), impl_->array_layers.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });

    // Check for gaps in layer indices
    for (size_t i = 0; i < impl_->array_layers.size(); ++i) {
      if (impl_->array_layers[i].first != static_cast<uint16_t>(i)) {
        LOG_F(WARNING,
          "TextureImportBuilder: array layer indices have gaps "
          "(expected {}, got {})",
          i, impl_->array_layers[i].first);
      }
    }

    // Extract paths
    std::vector<std::filesystem::path> paths;
    paths.reserve(impl_->array_layers.size());
    for (const auto& [idx, path] : impl_->array_layers) {
      paths.push_back(path);
    }

    if (impl_->custom_desc) {
      return ImportTextureArray(paths, desc, policy);
    }
    return ImportTextureArray(
      paths, impl_->preset.value_or(TexturePreset::kData), policy);
  }

  if (inferred_type == TextureType::kTexture3D
    && !impl_->depth_slices.empty()) {
    // Sort slices by index
    std::sort(impl_->depth_slices.begin(), impl_->depth_slices.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });

    // Check for gaps in slice indices
    for (size_t i = 0; i < impl_->depth_slices.size(); ++i) {
      if (impl_->depth_slices[i].first != static_cast<uint16_t>(i)) {
        LOG_F(WARNING,
          "TextureImportBuilder: depth slice indices have gaps "
          "(expected {}, got {})",
          i, impl_->depth_slices[i].first);
      }
    }

    // Extract paths
    std::vector<std::filesystem::path> paths;
    paths.reserve(impl_->depth_slices.size());
    for (const auto& [idx, path] : impl_->depth_slices) {
      paths.push_back(path);
    }

    if (impl_->custom_desc) {
      return ImportTexture3D(paths, desc, policy);
    }
    return ImportTexture3D(
      paths, impl_->preset.value_or(TexturePreset::kData), policy);
  }

  // Single source (2D texture)
  if (impl_->source_path) {
    auto bytes = ReadFileBytes(*impl_->source_path);
    if (!bytes) {
      return ::oxygen::Err(bytes.error());
    }

    desc.source_id = impl_->source_path->string();

    auto cooked = CookTexture(*bytes, desc, policy);
    if (!cooked) {
      LOG_F(WARNING, "TextureImportBuilder: cooking failed for '{}': {}",
        impl_->source_path->string(), to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    return ::oxygen::Ok(TextureImportResult {
      .payload = std::move(*cooked),
      .source_path = desc.source_id,
      .applied_preset = impl_->preset.value_or(TexturePreset::kData),
    });
  }

  if (impl_->source_memory) {
    desc.source_id = impl_->source_memory->second;

    if (!ValidateInputData(impl_->source_memory->first, desc.source_id)) {
      return ::oxygen::Err(TextureImportError::kCorruptedData);
    }

    auto cooked = CookTexture(impl_->source_memory->first, desc, policy);
    if (!cooked) {
      LOG_F(WARNING, "TextureImportBuilder: cooking failed for '{}': {}",
        desc.source_id, to_string(cooked.error()));
      return ::oxygen::Err(cooked.error());
    }

    return ::oxygen::Ok(TextureImportResult {
      .payload = std::move(*cooked),
      .source_path = desc.source_id,
      .applied_preset = impl_->preset.value_or(TexturePreset::kData),
    });
  }

  // No source provided
  LOG_F(WARNING, "TextureImportBuilder: no source provided");
  return ::oxygen::Err(TextureImportError::kFileNotFound);
}

} // namespace oxygen::content::import
