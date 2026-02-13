//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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

#include "TexturedCube/TextureLoadingService.h"

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
  import_status_.overall_progress = 0.0F;
  import_completed_ = false;
  import_report_ = {};

  if (!asset_loader_) {
    import_status_.message = "AssetLoader unavailable";
    return false;
  }

  if (settings.source_path.empty()) {
    LOG_F(
      WARNING, "TextureLoadingService: SubmitImport failed - No source path");
    import_status_.message = "No source path provided";
    return false;
  }

  LOG_F(INFO, "TextureLoadingService: Submitting import for '{}'",
    settings.source_path.string());

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
  options.with_content_hashing = settings.with_content_hashing;

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
  tuning.bc7_quality = IsBc7Format(output_format)
    ? static_cast<Bc7Quality>(settings.bc7_quality_idx)
    : Bc7Quality::kNone;
  tuning.flip_y_on_decode = settings.flip_y;
  tuning.force_rgba_on_decode = settings.force_rgba;

  // New props
  tuning.flip_normal_green = settings.flip_normal_green;
  tuning.exposure_ev = settings.exposure_ev;
  tuning.hdr_handling = static_cast<oxygen::content::import::HdrHandling>(
    settings.hdr_handling_idx);

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
    tuning.import_cubemap = true;

    // Check for Equirectangular layout (Index 5 in new UI)
    // Or old explicit kind kSkyboxEquirect (1) - keep for backward compat if
    // needed? We merged UI to use Kind 2 (Layout) + Layout Index 5.

    bool is_equirect = (settings.kind == ImportKind::kSkyboxEquirect)
      || (settings.kind == ImportKind::kSkyboxLayout
        && settings.layout_idx == 5);

    if (is_equirect) {
      tuning.equirect_to_cubemap = true;
      tuning.cubemap_face_size = static_cast<uint32_t>(settings.cube_face_size);
    }

    if (settings.kind == ImportKind::kSkyboxLayout
      && settings.layout_idx != 5) {
      tuning.cubemap_layout = CubeLayoutFromIndex(settings.layout_idx);
    }
  }

  oxygen::content::import::ImportRequest request {};
  request.source_path = settings.source_path;
  request.cooked_root = std::filesystem::absolute(settings.cooked_root);
  request.options = std::move(options);

  import_status_.message = "Submitting import...";

  {
    // Use stem() (filename without extension) to match assets regardless of
    // source/output extension
    std::string key = settings.source_path.stem().string();
    PendingMetadata pm;
    pm.settings = settings;
    pm.baseline_table_size = texture_table_.size();
    pending_metadata_[key] = pm;
    LOG_F(INFO,
      "TextureLoadingService: Added pending metadata for key '{}', baseline "
      "table size: {}, pending size: {}",
      key, pm.baseline_table_size, pending_metadata_.size());
  }

  const auto on_complete = [this, settings](
                             oxygen::content::import::ImportJobId /*job_id*/,
                             const ImportReport& report) {
    std::lock_guard lock(import_mutex_);
    import_report_ = report;
    import_completed_ = true;
    import_status_.in_flight = false;
    import_status_.overall_progress = 1.0F;
    import_status_.message
      = report.success ? "Import complete" : "Import failed";

    import_status_.message
      = report.success ? "Import complete" : "Import failed";

    if (report.success && !report.outputs.empty()) {
      LOG_F(INFO, "TextureLoadingService: Import report has {} outputs",
        report.outputs.size());
      for (const auto& out : report.outputs) {
        LOG_F(INFO, "TextureLoadingService: Output: path='{}', size={} bytes",
          out.path, out.size_bytes);
      }
    }

    if (report.success) {
      LOG_F(INFO, "TextureLoadingService: Import completed successfully");
    } else {
      LOG_F(ERROR, "TextureLoadingService: Import failed");
    }
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
      LOG_F(
        INFO, "TextureLoadingService: cooked root is empty, skipping refresh");
      if (error_message)
        *error_message = "Cooked root is empty";
      // Treat as success (nothing to load)
      cooked_entries_.clear();
      return true;
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
      LOG_F(WARNING,
        "TextureLoadingService: textures.table or textures.data missing in "
        "'{}'",
        normalized_root.string());
      cooked_entries_.clear();
      return true;
    }

    textures_table_path_ = normalized_root / *table_rel;
    textures_data_path_ = normalized_root / *data_rel;

    // If root changed, reload metadata
    if (normalized_root != cooked_root_) {
      LOG_F(INFO,
        "TextureLoadingService: Root changed to '{}', reloading metadata",
        normalized_root.string());
      cooked_root_ = normalized_root;
      texture_metadata_.clear();
      metadata_loaded_ = false;
    }

    if (!metadata_loaded_) {
      LoadTexturesJson();
    }

    size_t prev_size = texture_table_.size();
    texture_table_ = LoadPackedTable<TextureResourceDesc>(textures_table_path_);
    if (texture_table_.size() != prev_size) {
      LOG_F(INFO, "TextureLoadingService: Table size changed from {} to {}",
        prev_size, texture_table_.size());
    }
    cooked_entries_.clear();
    cooked_entries_.reserve(texture_table_.size());

    // Build a map of content_hash -> virtual_path/name
    std::unordered_map<uint64_t, std::string> hash_to_name;
    const auto assets = inspection.Assets();
    LOG_F(INFO, "TextureLoadingService: Inspection found {} assets in index",
      assets.size());

    std::unordered_set<uint64_t> table_hashes;
    for (const auto& desc : texture_table_) {
      table_hashes.insert(desc.content_hash);
    }

    bool promoted = false;
    for (const auto& asset : assets) {
      if (asset.descriptor_relpath.empty()) {
        continue;
      }

      // Read header to get content hash
      std::filesystem::path desc_path = cooked_root_ / asset.descriptor_relpath;
      oxygen::data::pak::AssetHeader header;
      bool header_read = false;

      std::ifstream file(desc_path, std::ios::binary);
      if (file) {
        if (file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
          header_read = true;
          hash_to_name[header.content_hash] = asset.virtual_path;
        }
      }

      if (header_read) {
        // We have a hash. Check if we have pending metadata for this asset.
        std::filesystem::path vpath(asset.virtual_path);
        std::string key = vpath.stem().string();

        std::lock_guard lock(import_mutex_);
        if (pending_metadata_.count(key)) {
          TextureMetadata meta;
          meta.settings = pending_metadata_[key].settings;
          meta.source_path = meta.settings.source_path.string();

          LOG_F(INFO,
            "TextureLoadingService: Promoting pending metadata for '{}' (hash: "
            "{:x}) via Asset Index",
            key, header.content_hash);
          texture_metadata_[header.content_hash] = std::move(meta);
          pending_metadata_.erase(key);
          promoted = true;
        }
      }
    }

    // HEURISTIC FALLBACK: If Assets index is empty (or we have leftover pending
    // metadata) and we have pending metadata and unmapped hashes in the table.
    if (!pending_metadata_.empty()) {
      std::lock_guard lock(import_mutex_);

      LOG_F(INFO,
        "TextureLoadingService: Fallback match check for {} pending items",
        pending_metadata_.size());

      for (auto p_it = pending_metadata_.begin();
        p_it != pending_metadata_.end();) {
        const auto& key = p_it->first;
        const auto& pm = p_it->second;

        std::vector<uint64_t> newly_appeared_hashes;
        std::vector<uint64_t> unmapped_hashes;

        for (size_t i = 0; i < texture_table_.size(); ++i) {
          // SKIP FALLBACK TEXTURE (Index 0 is always reserved for the engine
          // fallback)
          if (i == 0)
            continue;

          uint64_t hash = texture_table_[i].content_hash;
          bool is_mapped
            = (texture_metadata_.find(hash) != texture_metadata_.end());

          if (!is_mapped) {
            unmapped_hashes.push_back(hash);
            // It's "newly appeared" relative to THIS job if it's at index >=
            // baseline
            if (i >= pm.baseline_table_size) {
              newly_appeared_hashes.push_back(hash);
            }
          }
        }

        LOG_F(INFO,
          "TextureLoadingService: Key '{}' (baseline={}): newly_appeared={}, "
          "unmapped={}",
          key, pm.baseline_table_size, newly_appeared_hashes.size(),
          unmapped_hashes.size());

        bool match_found = false;
        // Scenario A: 1-to-1 match for a NEWLY appeared texture relative to
        // this job
        if (newly_appeared_hashes.size() == 1) {
          uint64_t hash = newly_appeared_hashes[0];
          TextureMetadata meta;
          meta.settings = pm.settings;
          meta.source_path = meta.settings.source_path.string();

          LOG_F(INFO,
            "TextureLoadingService: Promoting metadata for '{}' (matched newly "
            "appeared hash: {:x})",
            key, hash);
          texture_metadata_[hash] = std::move(meta);
          match_found = true;
        }
        // Scenario B: Deduplication - 0 new, but some unmapped existing ones
        else if (newly_appeared_hashes.empty() && unmapped_hashes.size() > 0) {
          // If there's only 1 unmapped hash in the whole table, it's our best
          // guess
          if (unmapped_hashes.size() == 1) {
            uint64_t hash = unmapped_hashes[0];
            TextureMetadata meta;
            meta.settings = pm.settings;
            meta.source_path = meta.settings.source_path.string();

            LOG_F(INFO,
              "TextureLoadingService: Promoting metadata for '{}' "
              "(deduplicated to only unmapped hash: {:x})",
              key, hash);
            texture_metadata_[hash] = std::move(meta);
            match_found = true;
          } else {
            LOG_F(WARNING,
              "TextureLoadingService: Ambiguous deduplication for '{}'. {} "
              "unmapped hashes exist.",
              key, unmapped_hashes.size());
          }
        } else if (newly_appeared_hashes.size() > 1) {
          LOG_F(WARNING,
            "TextureLoadingService: Ambiguous promotion for '{}'. {} new "
            "hashes appeared since baseline.",
            key, newly_appeared_hashes.size());
        }

        if (match_found) {
          p_it = pending_metadata_.erase(p_it);
          promoted = true;
        } else {
          ++p_it;
        }
      }
    }

    // Update known hashes for next time (still useful for general tracking)
    known_hashes_ = std::move(table_hashes);

    if (promoted) {
      SaveTexturesJson();
    }

    for (std::uint32_t i = 0; i < texture_table_.size(); ++i) {
      const auto& desc = texture_table_[i];
      std::string name;

      // Prefer metadata name/source
      if (texture_metadata_.count(desc.content_hash)) {
        name = texture_metadata_[desc.content_hash].source_path;
      } else if (auto it = hash_to_name.find(desc.content_hash);
        it != hash_to_name.end()) {
        name = it->second;
      } else {
        name = fmt::format("Texture_{}", i);
      }

      cooked_entries_.push_back(CookedTextureEntry {
        .index = i,
        .width = desc.width,
        .height = desc.height,
        .mip_levels = desc.mip_levels,
        .array_layers = desc.array_layers,
        .size_bytes = desc.size_bytes,
        .content_hash = desc.content_hash,
        .name = std::move(name),
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
    LOG_F(WARNING, "TextureLoadingService: refresh failed root='{}' error='{}'",
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

void TextureLoadingService::LoadTexturesJson()
{
  if (cooked_root_.empty()) {
    LOG_F(INFO, "TextureLoadingService: Skipping load, cooked_root is empty");
    return;
  }
  std::filesystem::path json_path = cooked_root_ / "textures.json";
  if (!std::filesystem::exists(json_path)) {
    LOG_F(INFO, "TextureLoadingService: No textures.json found at '{}'",
      json_path.string());
    metadata_loaded_ = true; // Mark as "processed" even if missing
    return;
  }

  LOG_F(INFO, "TextureLoadingService: Loading metadata from '{}'",
    json_path.string());
  try {
    std::ifstream file(json_path);
    if (!file.is_open()) {
      LOG_F(ERROR, "TextureLoadingService: Failed to open '{}' for reading",
        json_path.string());
      return;
    }
    nlohmann::json j;
    file >> j;

    size_t count = 0;
    if (j.contains("textures")) {
      for (const auto& item : j["textures"]) {
        uint64_t hash = item.value("content_hash", 0ULL);
        if (hash == 0) {
          // Try to read content_hash as a number even if it's large
          if (item.contains("content_hash")
            && item["content_hash"].is_number_integer()) {
            hash = item["content_hash"].get<uint64_t>();
          }
        }

        if (hash == 0)
          continue;

        TextureMetadata meta;
        meta.source_path = item.value("source_path", "");

        if (item.contains("settings")) {
          const auto& sets = item["settings"];
          meta.settings.source_path = meta.source_path; // Restore source
          // meta.settings.cooked_root = ...; // Not needed strictly for display
          meta.settings.kind = static_cast<ImportKind>(sets.value("kind", 0));
          meta.settings.output_format_idx = sets.value("output_format_idx", 0);
          meta.settings.generate_mips = sets.value("generate_mips", true);
          meta.settings.max_mip_levels = sets.value("max_mip_levels", 0);
          meta.settings.mip_filter_idx = sets.value("mip_filter_idx", 1);
          meta.settings.flip_y = sets.value("flip_y", false);
          meta.settings.force_rgba = sets.value("force_rgba", true);
          meta.settings.cube_face_size = sets.value("cube_face_size", 512);
          meta.settings.layout_idx = sets.value("layout_idx", 0);
          meta.settings.flip_normal_green
            = sets.value("flip_normal_green", false);
          meta.settings.exposure_ev = sets.value("exposure_ev", 0.0F);
          meta.settings.bc7_quality_idx = sets.value("bc7_quality_idx", 2);
          meta.settings.hdr_handling_idx = sets.value("hdr_handling_idx", 1);
        }
        texture_metadata_[hash] = std::move(meta);
        count++;
      }
    }
    metadata_loaded_ = true;
    LOG_F(INFO,
      "TextureLoadingService: Successfully loaded {} metadata entries", count);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "TextureLoadingService: Failed to parse textures.json: {}",
      e.what());
  }
}

void TextureLoadingService::SaveTexturesJson()
{
  if (cooked_root_.empty())
    return;
  std::filesystem::path json_path = cooked_root_ / "textures.json";

  nlohmann::json j;
  j["textures"] = nlohmann::json::array();

  for (const auto& [hash, meta] : texture_metadata_) {
    nlohmann::json item;
    item["content_hash"] = hash;
    item["source_path"] = meta.source_path;

    nlohmann::json sets;
    sets["kind"] = static_cast<int>(meta.settings.kind);
    sets["output_format_idx"] = meta.settings.output_format_idx;
    sets["generate_mips"] = meta.settings.generate_mips;
    sets["max_mip_levels"] = meta.settings.max_mip_levels;
    sets["mip_filter_idx"] = meta.settings.mip_filter_idx;
    sets["flip_y"] = meta.settings.flip_y;
    sets["force_rgba"] = meta.settings.force_rgba;
    sets["cube_face_size"] = meta.settings.cube_face_size;
    sets["layout_idx"] = meta.settings.layout_idx;
    sets["flip_normal_green"] = meta.settings.flip_normal_green;
    sets["exposure_ev"] = meta.settings.exposure_ev;
    sets["bc7_quality_idx"] = meta.settings.bc7_quality_idx;
    sets["hdr_handling_idx"] = meta.settings.hdr_handling_idx;

    item["settings"] = sets;
    j["textures"].push_back(item);
  }

  try {
    std::ofstream file(json_path);
    if (!file.is_open()) {
      LOG_F(ERROR, "TextureLoadingService: Failed to open '{}' for writing",
        json_path.string());
      return;
    }
    file << j.dump(2);
    LOG_F(INFO, "TextureLoadingService: Saved {} metadata entries to '{}'",
      texture_metadata_.size(), json_path.string());
  } catch (const std::exception& e) {
    LOG_F(ERROR, "TextureLoadingService: Failed to save textures.json: {}",
      e.what());
  }
}

auto TextureLoadingService::GetTextureMetadataJson(uint64_t hash) const
  -> std::string
{
  std::lock_guard lock(import_mutex_);
  auto it = texture_metadata_.find(hash);
  if (it == texture_metadata_.end()) {
    return "";
  }

  const auto& meta = it->second;
  nlohmann::json sets;
  sets["kind"] = static_cast<int>(meta.settings.kind);
  sets["output_format_idx"] = meta.settings.output_format_idx;
  sets["generate_mips"] = meta.settings.generate_mips;
  sets["max_mip_levels"] = meta.settings.max_mip_levels;
  sets["mip_filter_idx"] = meta.settings.mip_filter_idx;
  sets["flip_y"] = meta.settings.flip_y;
  sets["force_rgba"] = meta.settings.force_rgba;
  sets["cube_face_size"] = meta.settings.cube_face_size;
  sets["layout_idx"] = meta.settings.layout_idx;
  sets["flip_normal_green"] = meta.settings.flip_normal_green;
  sets["exposure_ev"] = meta.settings.exposure_ev;
  sets["bc7_quality_idx"] = meta.settings.bc7_quality_idx;
  sets["hdr_handling_idx"] = meta.settings.hdr_handling_idx;

  return sets.dump(2);
}

} // namespace oxygen::examples::textured_cube
