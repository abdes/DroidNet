//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Import/AsyncImportService.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::data {
class TextureResource;
} // namespace oxygen::data

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::examples::textured_cube {

//! Service for importing and browsing cooked textures.
/*!
 This class submits async import jobs that write to a loose cooked root,
 refreshes the textures table, and uploads cooked textures on demand.

 ### Features

 - Submits async import jobs via AsyncImportService
 - Refreshes cooked texture tables from a loose cooked root
 - Loads cooked textures by table index

 ### Usage

 ```cpp
 TextureLoadingService loader(asset_loader);
 loader.SubmitImport(settings);
 loader.RefreshCookedTextureEntries(cooked_root, nullptr);
 loader.StartLoadCookedTexture(entry_index,
   [](TextureLoadingService::LoadResult result) {
     if (result.success) {
       // Handle success.
     }
   });
 ```
*/
class TextureLoadingService final {
public:
  //! Import type for the async texture job.
  enum class ImportKind : std::uint8_t {
    kTexture2D = 0,
    kSkyboxEquirect = 1,
    kSkyboxLayout = 2,
  };

  //! Options for submitting an async texture import job.
  struct ImportSettings {
    std::filesystem::path source_path {};
    std::filesystem::path cooked_root {};
    ImportKind kind { ImportKind::kTexture2D };
    int output_format_idx { 0 };
    bool generate_mips { true };
    int max_mip_levels { 0 };
    int mip_filter_idx { 1 };
    bool flip_y { false };
    bool force_rgba { true };
    int cube_face_size { 512 };
    int layout_idx { 0 };
  };

  //! Status snapshot for an in-flight import.
  struct ImportStatus {
    bool in_flight { false };
    float overall_progress { 0.0f };
    std::string message {};
  };

  //! One entry from textures.table for display and selection.
  struct CookedTextureEntry {
    std::uint32_t index { 0U };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_levels { 0U };
    std::uint32_t array_layers { 0U };
    std::uint64_t size_bytes { 0U };
    std::uint64_t content_hash { 0U };
    oxygen::Format format { oxygen::Format::kUnknown };
    oxygen::TextureType texture_type { oxygen::TextureType::kTexture2D };
  };

  //! Result of loading a cooked texture.
  struct LoadResult {
    bool success { false };
    oxygen::content::ResourceKey resource_key { 0U };
    std::string status_message {};
    int width { 0 };
    int height { 0 };
    oxygen::TextureType texture_type { oxygen::TextureType::kTexture2D };
  };

  using LoadCallback = std::function<void(LoadResult)>;

  explicit TextureLoadingService(
    oxygen::observer_ptr<oxygen::content::IAssetLoader> asset_loader);

  //! Ensure the import service is stopped before destruction.
  ~TextureLoadingService();

  TextureLoadingService(const TextureLoadingService&) = delete;
  auto operator=(const TextureLoadingService&)
    -> TextureLoadingService& = delete;
  TextureLoadingService(TextureLoadingService&&) = delete;
  auto operator=(TextureLoadingService&&) -> TextureLoadingService& = delete;

  //! Submit a texture import job that writes to a cooked root.
  auto SubmitImport(const ImportSettings& settings) -> bool;

  //! Consume a completed import report (if available).
  auto ConsumeImportReport(oxygen::content::import::ImportReport& report)
    -> bool;

  //! Get the current import status snapshot.
  [[nodiscard]] auto GetImportStatus() const -> ImportStatus;

  //! Refresh the cooked texture table from a cooked root.
  auto RefreshCookedTextureEntries(const std::filesystem::path& cooked_root,
    std::string* error_message) -> bool;

  //! Get the current cooked texture entries.
  [[nodiscard]] auto GetCookedTextureEntries() const
    -> std::span<const CookedTextureEntry>;

  //! Begin loading a cooked texture by table index.
  auto StartLoadCookedTexture(
    std::uint32_t entry_index, LoadCallback on_complete) -> void;

private:
  oxygen::observer_ptr<oxygen::content::IAssetLoader> asset_loader_;
  oxygen::content::import::AsyncImportService import_service_ {};

  mutable std::mutex import_mutex_ {};
  ImportStatus import_status_ {};
  bool import_completed_ { false };
  oxygen::content::import::ImportReport import_report_ {};

  std::filesystem::path cooked_root_ {};
  std::filesystem::path textures_table_path_ {};
  std::filesystem::path textures_data_path_ {};
  std::vector<oxygen::data::pak::TextureResourceDesc> texture_table_ {};
  std::vector<CookedTextureEntry> cooked_entries_ {};
};

} // namespace oxygen::examples::textured_cube
