//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "TextureLoadingService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Import/ImportOptions.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>

namespace {

using oxygen::ColorSpace;
using oxygen::Format;
using oxygen::TextureType;
using oxygen::content::LooseCookedInspection;
using oxygen::content::import::Bc7Quality;
using oxygen::content::import::CubeMapImageLayout;
using oxygen::content::import::ImportContentFlags;
using oxygen::content::import::ImportOptions;
using oxygen::content::import::ImportReport;
using oxygen::content::import::MipFilter;
using oxygen::content::import::MipPolicy;
using oxygen::content::import::ProgressEvent;
using oxygen::content::import::TextureIntent;
using oxygen::data::loose_cooked::v1::FileKind;
using oxygen::data::pak::TextureResourceDesc;

auto IsHdrPath(const std::filesystem::path& path) -> bool
{
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return (ext == ".hdr") || (ext == ".exr");
}

auto FormatFromIndex(const int idx) -> Format
{
  switch (idx) {
  case 0:
    return Format::kRGBA8UNormSRGB;
  case 1:
    return Format::kBC7UNormSRGB;
  case 2:
    return Format::kRGBA16Float;
  case 3:
    return Format::kRGBA32Float;
  default:
    return Format::kRGBA8UNormSRGB;
  }
}

auto IsSrgbFormat(const Format format) -> bool
{
  return format == Format::kRGBA8UNormSRGB || format == Format::kBC7UNormSRGB
    || format == Format::kBGRA8UNormSRGB || format == Format::kBC1UNormSRGB
    || format == Format::kBC2UNormSRGB || format == Format::kBC3UNormSRGB;
}

auto IsBc7Format(const Format format) -> bool
{
  return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
}

auto CubeLayoutFromIndex(const int idx) -> CubeMapImageLayout
{
  switch (idx) {
  case 0:
    return CubeMapImageLayout::kAuto;
  case 1:
    return CubeMapImageLayout::kHorizontalCross;
  case 2:
    return CubeMapImageLayout::kVerticalCross;
  case 3:
    return CubeMapImageLayout::kHorizontalStrip;
  case 4:
    return CubeMapImageLayout::kVerticalStrip;
  default:
    return CubeMapImageLayout::kAuto;
  }
}

auto MipFilterFromIndex(const int idx) -> MipFilter
{
  switch (idx) {
  case 0:
    return MipFilter::kBox;
  case 1:
    return MipFilter::kKaiser;
  case 2:
    return MipFilter::kLanczos;
  default:
    return MipFilter::kKaiser;
  }
}

auto FindFileRelPath(const LooseCookedInspection& inspection,
  const FileKind kind) -> std::optional<std::string>
{
  for (const auto& entry : inspection.Files()) {
    if (entry.kind == kind) {
      return entry.relpath;
    }
  }
  return std::nullopt;
}

template <typename T>
auto LoadPackedTable(const std::filesystem::path& table_path) -> std::vector<T>
{
  std::ifstream stream(table_path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open table file");
  }

  stream.seekg(0, std::ios::end);
  const auto size_bytes = static_cast<std::size_t>(stream.tellg());
  stream.seekg(0, std::ios::beg);

  if (size_bytes == 0) {
    return {};
  }

  if (size_bytes % sizeof(T) != 0) {
    throw std::runtime_error("table size is not a multiple of entry size");
  }

  const auto count = size_bytes / sizeof(T);
  std::vector<T> entries(count);
  stream.read(reinterpret_cast<char*>(entries.data()),
    static_cast<std::streamsize>(size_bytes));
  if (!stream) {
    throw std::runtime_error("failed to read table file");
  }

  return entries;
}

} // namespace

namespace oxygen::examples::textured_cube {

TextureLoadingService::TextureLoadingService(
  oxygen::observer_ptr<oxygen::content::IAssetLoader> asset_loader)
  : asset_loader_(asset_loader)
{
}

/*!
 Ensure the import service is stopped before destruction.

 @note If the service is still running, it is stopped here to satisfy the
       AsyncImportService contract.
*/
TextureLoadingService::~TextureLoadingService() { import_service_.Stop(); }

auto TextureLoadingService::SubmitImport(const ImportSettings& settings) -> bool
{
  std::lock_guard lock(import_mutex_);

  import_status_ = {};
  import_status_.message = "Preparing import...";
  import_status_.in_flight = false;
  import_status_.overall_progress = 0.0f;
  import_completed_ = false;
  import_report_ = {};

  if (!asset_loader_) {
    import_status_.message = "AssetLoader unavailable";
    return false;
  }

  if (settings.source_path.empty()) {
    import_status_.message = "No source path provided";
    return false;
  }

  if (settings.cooked_root.empty()) {
    import_status_.message = "No cooked root provided";
    return false;
  }

  if (settings.kind == ImportKind::kSkyboxEquirect
    && (settings.cube_face_size % 256) != 0) {
    import_status_.message = "Cube face size must be a multiple of 256";
    return false;
  }

  const auto output_format = FormatFromIndex(settings.output_format_idx);
  const bool is_hdr_source = IsHdrPath(settings.source_path);

  ImportOptions options {};
  options.import_content = ImportContentFlags::kTextures;

  auto& tuning = options.texture_tuning;
  tuning.enabled = true;
  if (!settings.generate_mips) {
    tuning.mip_policy = MipPolicy::kNone;
  } else if (settings.max_mip_levels > 0) {
    tuning.mip_policy = MipPolicy::kMaxCount;
    tuning.max_mip_levels = static_cast<std::uint8_t>(settings.max_mip_levels);
  } else {
    tuning.mip_policy = MipPolicy::kFullChain;
  }
  tuning.mip_filter = MipFilterFromIndex(settings.mip_filter_idx);
  tuning.color_output_format = output_format;
  tuning.data_output_format = output_format;
  tuning.bc7_quality
    = IsBc7Format(output_format) ? Bc7Quality::kDefault : Bc7Quality::kNone;
  tuning.flip_y_on_decode = settings.flip_y;
  tuning.force_rgba_on_decode = settings.force_rgba;

  if (settings.kind == ImportKind::kTexture2D) {
    tuning.intent = TextureIntent::kAlbedo;
    tuning.source_color_space
      = IsSrgbFormat(output_format) ? ColorSpace::kSRGB : ColorSpace::kLinear;
  } else {
    tuning.intent
      = is_hdr_source ? TextureIntent::kHdrEnvironment : TextureIntent::kData;
    tuning.source_color_space
      = is_hdr_source ? ColorSpace::kLinear : ColorSpace::kSRGB;
    tuning.import_cubemap = true;
    if (settings.kind == ImportKind::kSkyboxEquirect) {
      tuning.equirect_to_cubemap = true;
      tuning.cubemap_face_size = static_cast<uint32_t>(settings.cube_face_size);
    }
    if (settings.kind == ImportKind::kSkyboxLayout) {
      tuning.cubemap_layout = CubeLayoutFromIndex(settings.layout_idx);
    }
  }

  oxygen::content::import::ImportRequest request {};
  request.source_path = settings.source_path;
  request.cooked_root = std::filesystem::absolute(settings.cooked_root);
  request.options = std::move(options);

  import_status_.message = "Submitting import...";

  const auto on_complete
    = [this](oxygen::content::import::ImportJobId /*job_id*/,
        const ImportReport& report) {
        std::lock_guard lock(import_mutex_);
        import_report_ = report;
        import_completed_ = true;
        import_status_.in_flight = false;
        import_status_.overall_progress = 1.0f;
        import_status_.message
          = report.success ? "Import complete" : "Import failed";
      };

  const auto on_progress = [this](const ProgressEvent& progress) {
    std::lock_guard lock(import_mutex_);
    import_status_.in_flight = true;
    import_status_.overall_progress = progress.header.overall_progress;
    if (!progress.header.message.empty()) {
      import_status_.message = progress.header.message;
    }
  };

  import_status_.in_flight = true;
  const auto job_id = import_service_.SubmitImport(
    std::move(request), on_complete, on_progress);
  if (job_id == oxygen::content::import::kInvalidJobId) {
    import_status_.in_flight = false;
    import_status_.message = "Import rejected (service unavailable)";
    return false;
  }

  return true;
}

auto TextureLoadingService::ConsumeImportReport(ImportReport& report) -> bool
{
  std::lock_guard lock(import_mutex_);
  if (!import_completed_) {
    return false;
  }

  report = import_report_;
  import_completed_ = false;
  return true;
}

auto TextureLoadingService::GetImportStatus() const -> ImportStatus
{
  std::lock_guard lock(import_mutex_);
  return import_status_;
}

auto TextureLoadingService::RefreshCookedTextureEntries(
  const std::filesystem::path& cooked_root, std::string* error_message) -> bool
{
  try {
    LOG_F(
      INFO, "TextureLoadingService: refresh root='{}'", cooked_root.string());
    if (cooked_root.empty()) {
      throw std::runtime_error("Cooked root is empty");
    }

    std::error_code ec;
    auto normalized_root = std::filesystem::absolute(cooked_root, ec);
    if (ec) {
      throw std::runtime_error("Failed to resolve cooked root path");
    }
    normalized_root = normalized_root.lexically_normal();

    LooseCookedInspection inspection;
    inspection.LoadFromRoot(normalized_root);

    const auto table_rel
      = FindFileRelPath(inspection, FileKind::kTexturesTable);
    const auto data_rel = FindFileRelPath(inspection, FileKind::kTexturesData);
    if (!table_rel.has_value() || !data_rel.has_value()) {
      throw std::runtime_error("textures.table or textures.data missing");
    }

    textures_table_path_ = normalized_root / *table_rel;
    textures_data_path_ = normalized_root / *data_rel;
    cooked_root_ = normalized_root;

    texture_table_ = LoadPackedTable<TextureResourceDesc>(textures_table_path_);
    cooked_entries_.clear();
    cooked_entries_.reserve(texture_table_.size());

    for (std::uint32_t i = 0; i < texture_table_.size(); ++i) {
      const auto& desc = texture_table_[i];
      cooked_entries_.push_back(CookedTextureEntry {
        .index = i,
        .width = desc.width,
        .height = desc.height,
        .mip_levels = desc.mip_levels,
        .array_layers = desc.array_layers,
        .size_bytes = desc.size_bytes,
        .content_hash = desc.content_hash,
        .format = static_cast<Format>(desc.format),
        .texture_type = static_cast<TextureType>(desc.texture_type),
      });
    }
    LOG_F(INFO, "TextureLoadingService: refresh complete entries={} table='{}'",
      cooked_entries_.size(), textures_table_path_.string());
  } catch (const std::exception& e) {
    if (error_message != nullptr) {
      *error_message = e.what();
    }
    LOG_F(ERROR, "TextureLoadingService: refresh failed root='{}' error='{}'",
      cooked_root.string(), e.what());
    return false;
  }

  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

auto TextureLoadingService::GetCookedTextureEntries() const
  -> std::span<const CookedTextureEntry>
{
  return cooked_entries_;
}

auto TextureLoadingService::StartLoadCookedTexture(
  const std::uint32_t entry_index, LoadCallback on_complete) -> void
{
  LoadResult result;

  if (!asset_loader_) {
    result.status_message = "AssetLoader unavailable";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  if (entry_index >= texture_table_.size()) {
    result.status_message = "Texture index out of range";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  if (textures_data_path_.empty()) {
    result.status_message = "textures.data is not available";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  auto desc = texture_table_[entry_index];
  std::ifstream data_stream(textures_data_path_, std::ios::binary);
  if (!data_stream) {
    result.status_message = "Failed to open textures.data";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  data_stream.seekg(
    static_cast<std::streamoff>(desc.data_offset), std::ios::beg);
  if (!data_stream) {
    result.status_message = "Failed to seek textures.data";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  std::vector<std::uint8_t> payload(desc.size_bytes);
  data_stream.read(reinterpret_cast<char*>(payload.data()),
    static_cast<std::streamsize>(payload.size()));
  if (!data_stream) {
    result.status_message = "Failed to read texture payload";
    if (on_complete) {
      on_complete(std::move(result));
    }
    return;
  }

  desc.data_offset
    = static_cast<oxygen::data::pak::OffsetT>(sizeof(TextureResourceDesc));

  auto packed = std::make_shared<std::vector<std::uint8_t>>();
  packed->resize(sizeof(TextureResourceDesc) + payload.size());
  std::memcpy(packed->data(), &desc, sizeof(TextureResourceDesc));
  std::memcpy(packed->data() + sizeof(TextureResourceDesc), payload.data(),
    payload.size());

  const auto resource_key = asset_loader_->MintSyntheticTextureKey();

  asset_loader_->StartLoadTexture(
    oxygen::content::CookedResourceData<oxygen::data::TextureResource> {
      .key = resource_key,
      .bytes = std::span<const std::uint8_t>(packed->data(), packed->size()),
    },
    [on_complete = std::move(on_complete), packed,
      width = static_cast<int>(desc.width),
      height = static_cast<int>(desc.height),
      texture_type = static_cast<TextureType>(desc.texture_type), resource_key](
      std::shared_ptr<oxygen::data::TextureResource> tex) mutable {
      LoadResult callback_result;
      callback_result.resource_key = resource_key;
      callback_result.width = width;
      callback_result.height = height;
      callback_result.texture_type = texture_type;

      if (!tex) {
        callback_result.status_message = "Texture upload failed";
      } else {
        callback_result.success = true;
        callback_result.status_message = "Loaded cooked texture";
      }

      if (on_complete) {
        on_complete(std::move(callback_result));
      }
    });
}

} // namespace oxygen::examples::textured_cube
