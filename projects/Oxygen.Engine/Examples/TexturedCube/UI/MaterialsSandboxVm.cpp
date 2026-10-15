//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <system_error>
#include <utility>

#include "DemoShell/Services/FileBrowserService.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/TextureLoadingService.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::examples::textured_cube::ui {

using oxygen::examples::FileBrowserService;

MaterialsSandboxVm::MaterialsSandboxVm(
  observer_ptr<TextureLoadingService> texture_service,
  observer_ptr<FileBrowserService> file_browser)
  : texture_service_(texture_service)
  , file_browser_(file_browser)
{
  // Initialize defaults from file browser service if available
  if (file_browser_) {
    const auto roots = file_browser_->GetContentRoots();
    content_root_ = roots.content_root;
    if (!roots.cooked_root.empty()) {
      const auto path_str = roots.cooked_root.string();
      // Ensure null termination and size fit
      if (path_str.size() < import_state_.cooked_root.size()) {
        std::ranges::copy(path_str, import_state_.cooked_root.begin());
        import_state_.cooked_root[path_str.size()] = '\0';
      }
    }

    // Default source path to Content/images (or Textures if it existed)
    if (!roots.content_root.empty()) {
      // User mentioned "Content/Textures", but we found "Content/images".
      // Let's try to map to what exists.
      auto source_dir = roots.content_root / "images";
      // If images doesn't exist, fall back to content root.
      std::error_code ec;
      if (!std::filesystem::exists(source_dir, ec)) {
        source_dir = roots.content_root;
      }

      const auto path_str = source_dir.string();
      if (path_str.size() < import_state_.source_path.size()) {
        std::ranges::copy(path_str, import_state_.source_path.begin());
        import_state_.source_path[path_str.size()] = '\0';
      }
    }
  }
}

MaterialsSandboxVm::~MaterialsSandboxVm() = default;

auto MaterialsSandboxVm::StartImportFlow() -> void
{
  // 1. Reset State
  import_state_.status_message.clear();
  import_state_.progress = 0.0F;
  import_state_.workflow_state = ImportState::WorkflowState::Idle;
  import_state_.last_import_success = false;

  // Default options
  import_state_.usage = ImportState::Usage::kAuto;
  import_state_.compress = true;
  import_state_.compute_hash = true;

  // Trigger Browser
  BrowseForSourcePath();
}

auto MaterialsSandboxVm::CancelImport() -> void
{
  import_state_.workflow_state = ImportState::WorkflowState::Idle;
  import_state_.status_message.clear();
  import_state_.progress = 0.0F;
}

auto MaterialsSandboxVm::OnFileSelected(const std::filesystem::path& path)
  -> void
{
  if (browse_mode_ == BrowseMode::kSourcePath) {
    // Called when browser returns a file
    std::string path_str = path.string();
    size_t len
      = std::min(path_str.size(), import_state_.source_path.size() - 1);
    std::copy_n(path_str.begin(), len, import_state_.source_path.begin());
    import_state_.source_path[len] = '\0';

    // Auto-configure
    UpdateImportSettingsFromUsage();

    // Transition to Configuring
    import_state_.workflow_state = ImportState::WorkflowState::Configuring;
  } else if (browse_mode_ == BrowseMode::kCookedRoot) {
    std::string path_str = path.string();
    size_t len
      = std::min(path_str.size(), import_state_.cooked_root.size() - 1);
    std::copy_n(path_str.begin(), len, import_state_.cooked_root.begin());
    import_state_.cooked_root[len] = '\0';
  }
}

auto MaterialsSandboxVm::UpdateImportSettingsFromUsage() -> void
{
  std::string source_path = import_state_.source_path.data();
  std::filesystem::path path(source_path);
  std::string ext = path.extension().string();
  // ToLower
  std::ranges::transform(
    ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });

  std::string filename = path.filename().string();
  std::ranges::transform(filename, filename.begin(),
    [](unsigned char c) { return std::tolower(c); });

  auto effective_usage = import_state_.usage;

  // 1. Auto-Detect
  if (effective_usage == ImportState::Usage::kAuto) {
    if (ext == ".hdr" || ext == ".exr") {
      effective_usage = ImportState::Usage::kHdrEnvironment;
    } else if (filename.contains("_normal")) {
      effective_usage = ImportState::Usage::kNormal;
    } else if (filename.contains("_ui")) {
      effective_usage = ImportState::Usage::kUi;
    } else {
      effective_usage = ImportState::Usage::kAlbedo;
    }
  }

  import_state_.generate_mips = true;
  import_state_.max_mip_levels = 0; // All
  import_state_.flip_y = false;
  import_state_.force_rgba = true;

  // Output formats index TextureLoadingService's list: RGBA8 sRGB, BC7 sRGB,
  // RGBA16F, RGBA32F.
  switch (effective_usage) {
  case ImportState::Usage::kHdrEnvironment:
    import_state_.import_kind = 2; // Skybox (Layout)
    import_state_.layout_idx = 5; // Equirectangular
    import_state_.output_format_idx = 2; // RGBA16F
    break;

  case ImportState::Usage::kNormal:
    // The list has no linear 8-bit or BC5 format; normals need linear data.
    import_state_.import_kind = 0; // Texture 2D
    import_state_.output_format_idx = 2; // RGBA16F
    break;

  case ImportState::Usage::kUi:
    import_state_.import_kind = 0; // Texture 2D
    import_state_.output_format_idx = 0; // RGBA8 SRGB
    import_state_.generate_mips = false;
    break;

  case ImportState::Usage::kAlbedo:
  default:
    import_state_.import_kind = 0; // Texture 2D
    if (import_state_.compress) {
      import_state_.output_format_idx = 1; // BC7 SRGB
    } else {
      import_state_.output_format_idx = 0; // RGBA8 SRGB
    }
    break;
  }
}

auto MaterialsSandboxVm::RequestImport() -> void
{
  if (!texture_service_) {
    return;
  }

  TextureLoadingService::ImportSettings settings {
    .source_path = std::filesystem::path(import_state_.source_path.data()),
    .cooked_root = std::filesystem::path(import_state_.cooked_root.data()),
    .kind
    = static_cast<TextureLoadingService::ImportKind>(import_state_.import_kind),
    .output_format_idx = import_state_.output_format_idx,
    .generate_mips = import_state_.generate_mips,
    .max_mip_levels = import_state_.max_mip_levels,
    .mip_filter_idx = import_state_.mip_filter_idx,
    .flip_y = import_state_.flip_y,
    .force_rgba = import_state_.force_rgba,
    .cube_face_size = import_state_.cube_face_size,
    .layout_idx = import_state_.layout_idx,
    .with_content_hashing = import_state_.compute_hash,
    .flip_normal_green = import_state_.flip_normal_green,
    .exposure_ev = import_state_.exposure_ev,
    .bc7_quality_idx = import_state_.bc7_quality_idx,
    .hdr_handling_idx = import_state_.hdr_handling_idx,
  };

  if (!texture_service_->SubmitImport(settings)) {
    cube_needs_rebuild_ = true;
  }
}

auto MaterialsSandboxVm::RequestRefresh() -> void { refresh_requested_ = true; }

auto MaterialsSandboxVm::BrowseForSourcePath() -> void
{
  if (file_browser_) {
    FileBrowserConfig config;
    config.title = "Select Source Texture";
    // config.initial_directory = ...
    config.filters.push_back({ .description = "Image Files",
      .extensions = { ".png", ".jpg", ".tga", ".bmp", ".hdr", ".exr" } });

    if (import_state_.source_path[0] != '\0') {
      config.initial_directory
        = std::filesystem::path(import_state_.source_path.data()).parent_path();
    }

    active_request_id_ = file_browser_->Open(config);
    browse_mode_ = BrowseMode::kSourcePath;
  }
}

auto MaterialsSandboxVm::BrowseForCookedRoot() -> void
{
  if (file_browser_) {
    FileBrowserConfig config;
    config.title = "Select Cooked Root Directory";
    config.select_directory = true;
    if (import_state_.cooked_root[0] != '\0') {
      config.initial_directory
        = std::filesystem::path(import_state_.cooked_root.data());
    }
    active_request_id_ = file_browser_->Open(config);
    browse_mode_ = BrowseMode::kCookedRoot;
  }
}

auto MaterialsSandboxVm::SelectTextureForSlot(
  uint32_t entry_index, bool is_sphere) -> bool
{
  if (!texture_service_) {
    return false;
  }

  import_state_.status_message = "Loading cooked texture...";
  // import_state_.in_flight = true;
  import_state_.progress = 0.0F;

  texture_service_->StartLoadCookedTexture(entry_index,
    [this, entry_index, is_sphere](
      const TextureLoadingService::LoadResult& result) {
      import_state_.status_message = result.status_message;
      // import_state_.in_flight = false;

      if (!result.success) {
        import_state_.progress = 0.0F;
        return;
      }

      import_state_.progress = 1.0F;

      if (is_sphere) {
        sphere_texture_.mode = TextureIndexMode::kCustom;
        sphere_texture_.resource_index = entry_index;
        sphere_texture_.resource_key = result.resource_key;
      } else {
        cube_texture_.mode = TextureIndexMode::kCustom;
        cube_texture_.resource_index = entry_index;
        cube_texture_.resource_key = result.resource_key;
      }
      cube_needs_rebuild_ = true;
    });

  return true;
}

auto MaterialsSandboxVm::SetOnSkyboxSelected(
  std::function<void(oxygen::content::ResourceKey)> callback) -> void
{
  on_skybox_selected_ = std::move(callback);
}

auto MaterialsSandboxVm::SelectSkybox(uint32_t entry_index,
  std::function<void(oxygen::content::ResourceKey)> on_loaded) -> bool
{

  import_state_.status_message = "Loading skybox...";
  // import_state_.in_flight = true; // Use status message or dedicated flag if
  // needed for Skybox loading? Skybox loading is separate from Import Flow.
  // Should we use the same state?
  // If we repurpose ImportState for generic background tasks, we can use
  // WorkflowState::Importing (or Loading). But Skybox loading doesn't use the
  // Import Config UI. For now, let's just NOT use in_flight, relying on
  // callback. Or add a `loading_skybox` flag if needed for UI disabling. The UI
  // checks `in_flight` to disable buttons. Let's map "Loading" to a generic
  // busy state?

  // Quick fix: Set progress to 0 but don't change Import Workflow State if it's
  // unrelated? The UI currently disables based on `is_importing` which checks
  // `Importing`. Skybox loading was disabling the import modal? Let's ignore it
  // for the Inline Import Flow for now, as that flow is about importing NEW
  // textures. Skybox loading is about loading EXISTING cooked textures.

  import_state_.progress = 0.0F;

  texture_service_->StartLoadCookedTexture(entry_index,
    [this, on_loaded = std::move(on_loaded)](
      const TextureLoadingService::LoadResult& result) {
      if (result.success) {
        if (on_loaded) {
          on_loaded(result.resource_key);
        } else if (on_skybox_selected_) {
          on_skybox_selected_(result.resource_key);
        }
        import_state_.status_message = "Skybox loaded";
      } else {
        import_state_.status_message = result.status_message;
      }
      // import_state_.in_flight = false;
      import_state_.progress = 1.0F;
    });

  return true;
}

auto MaterialsSandboxVm::SetUseCustomMaterial(const bool enabled) -> void
{
  if (use_custom_material_ == enabled) {
    return;
  }
  use_custom_material_ = enabled;
  cube_needs_rebuild_ = true;
}

auto MaterialsSandboxVm::GetSelectedCustomMaterialKey() const
  -> std::optional<oxygen::data::AssetKey>
{
  return selected_custom_material_key_;
}

auto MaterialsSandboxVm::ClearSelectedCustomMaterial() -> void
{
  if (!selected_custom_material_key_.has_value()) {
    return;
  }
  selected_custom_material_key_.reset();
  cube_needs_rebuild_ = true;
}

auto MaterialsSandboxVm::SelectCustomMaterialByEntryIndex(
  const std::size_t entry_index) -> bool
{
  if (entry_index >= cooked_material_entries_.size()) {
    return false;
  }

  const auto key = cooked_material_entries_[entry_index].key;
  if (selected_custom_material_key_.has_value()
    && selected_custom_material_key_.value() == key) {
    return true;
  }

  selected_custom_material_key_ = key;
  cube_needs_rebuild_ = true;
  return true;
}

auto MaterialsSandboxVm::Update() -> void
{
  if (!texture_service_) {
    return;
  }

  // 1. Refresh if requested
  if (refresh_requested_) {
    HandleRefresh();
    refresh_requested_ = false;
  }

  // 2. Poll status
  UpdateImportStatus();

  // Handle File Browser results
  if (file_browser_ && browse_mode_ != BrowseMode::kNone) {
    if (const auto result = file_browser_->ConsumeResult(active_request_id_)) {
      if (result->kind == FileBrowserService::ResultKind::kSelected) {
        OnFileSelected(result->path);
      }
      browse_mode_ = BrowseMode::kNone;
      active_request_id_ = 0;
    } else if (!file_browser_->IsOpen()) {
      browse_mode_ = BrowseMode::kNone;
      active_request_id_ = 0;
    }
  }

  // 3. Consume report (if any finished)
  oxygen::content::import::ImportReport report;
  if (texture_service_->ConsumeImportReport(report)) {
    if (!report.success && !report.diagnostics.empty()) {
      import_state_.status_message = report.diagnostics.front().message;
      // import_state_.in_flight = false;
      import_state_.workflow_state = ImportState::WorkflowState::Finished;
      import_state_.last_import_success = false;
      import_state_.progress = 1.0F;
    } else {
      // Success case?
      // Report success handled below.
    }

    std::string error;
    const bool refreshed = texture_service_->RefreshCookedTextureEntries(
      report.cooked_root, &error);
    UpdateCookedMaterialEntries(report.cooked_root);
    if (refreshed) {
      UpdateCookedEntries();
      import_state_.status_message = "Import Successful";
      // import_state_.in_flight = false;
      import_state_.workflow_state
        = ImportState::WorkflowState::Idle; // Use Idle to auto-close or
                                            // Finished to show success
      // User requested: "After Import completes... Hide inline section"
      // So Idle is correct to hide it.

      // import_state_.last_import_success = true; // Only if referencing
      // Finished import_state_.workflow_state =
      // ImportState::WorkflowState::Finished;

      // Auto-hide on success:
      import_state_.workflow_state = ImportState::WorkflowState::Idle;

      import_state_.progress = 1.0F;
    } else {
      import_state_.status_message = error;
      // import_state_.in_flight = false;
      import_state_.workflow_state = ImportState::WorkflowState::Finished;
      import_state_.last_import_success = false;
      import_state_.progress = 1.0F;
    }
  }
}

auto MaterialsSandboxVm::UpdateImportStatus() -> void
{
  const auto status = texture_service_->GetImportStatus();
  if (!status.message.empty()) {
    import_state_.status_message = status.message;
    // import_state_.in_flight = status.in_flight;
    import_state_.progress = status.overall_progress;
  }
}

auto MaterialsSandboxVm::HandleRefresh() -> void
{
  const auto root_path
    = std::filesystem::path(import_state_.cooked_root.data());
  LOG_F(INFO, "TexturedCube: refresh requested root='{}'", root_path.string());

  std::string error;
  if (texture_service_->RefreshCookedTextureEntries(root_path, &error)) {
    UpdateCookedEntries();
    UpdateCookedMaterialEntries(root_path);
    import_state_.status_message = "Cooked root refreshed";
  } else {
    UpdateCookedMaterialEntries(root_path);
    LOG_F(INFO, "TexturedCube: refresh failed root='{}' error='{}'",
      root_path.string(), error);
    import_state_.status_message = error;
  }
  // import_state_.in_flight = false;
  import_state_.progress = 0.0F;
}

auto MaterialsSandboxVm::UpdateCookedEntries() -> void
{
  if (!texture_service_) {
    return;
  }

  const auto& service_entries = texture_service_->GetCookedTextureEntries();
  cooked_entries_.clear();
  cooked_entries_.reserve(service_entries.size());

  for (const auto& se : service_entries) {
    std::string display_name = se.name;
    if (!content_root_.empty()) {
      std::error_code ec;
      auto rel = std::filesystem::proximate(display_name, content_root_, ec);
      if (!ec) {
        display_name = rel.string();
      }
    }

    cooked_entries_.push_back(CookedTextureEntry {
      .index = se.index,
      .width = se.width,
      .height = se.height,
      .mip_levels = se.mip_levels,
      .array_layers = se.array_layers,
      .size_bytes = se.size_bytes,
      .content_hash = se.content_hash,
      .name = std::move(display_name),
      .format = se.format,
      .texture_type = se.texture_type,
    });
  }

  LOG_F(INFO, "TexturedCube: refresh completed entries={} ",
    cooked_entries_.size());
  ReloadAssignedTextures();
}

auto MaterialsSandboxVm::ReloadAssignedTextures() -> void
{
  const auto reload = [this](TextureSlotState& slot) -> void {
    if (slot.mode != TextureIndexMode::kCustom) {
      return;
    }
    texture_service_->StartLoadCookedTexture(slot.resource_index,
      [this, &slot](const TextureLoadingService::LoadResult& result) -> void {
        if (!result.success) {
          LOG_F(WARNING,
            "TexturedCube: assigned texture entry {} cannot be loaded after "
            "refresh: {}",
            slot.resource_index, result.status_message);
          slot.mode = TextureIndexMode::kFallback;
          slot.resource_key = oxygen::content::ResourceKey { 0U };
        } else {
          slot.resource_key = result.resource_key;
        }
        cube_needs_rebuild_ = true;
      });
  };
  reload(sphere_texture_);
  reload(cube_texture_);
}

auto MaterialsSandboxVm::UpdateCookedMaterialEntries(
  const std::filesystem::path& cooked_root) -> void
{
  cooked_material_entries_.clear();
  bool selection_cleared = false;

  if (cooked_root.empty()) {
    selection_cleared = selected_custom_material_key_.has_value();
    selected_custom_material_key_.reset();
    if (selection_cleared && use_custom_material_) {
      cube_needs_rebuild_ = true;
    }
    return;
  }

  try {
    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromRoot(cooked_root);

    constexpr auto kMaterialAssetType
      = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type != kMaterialAssetType) {
        continue;
      }

      cooked_material_entries_.push_back(CookedMaterialEntry {
        .key = asset.key,
        .virtual_path = asset.virtual_path,
        .descriptor_relpath = asset.descriptor_relpath,
        .descriptor_size = asset.descriptor_size,
      });
    }

    std::ranges::sort(cooked_material_entries_,
      [](const CookedMaterialEntry& lhs, const CookedMaterialEntry& rhs) {
        if (lhs.virtual_path == rhs.virtual_path) {
          return lhs.descriptor_relpath < rhs.descriptor_relpath;
        }
        return lhs.virtual_path < rhs.virtual_path;
      });

    if (selected_custom_material_key_.has_value()) {
      const auto it = std::ranges::find_if(cooked_material_entries_,
        [selected = selected_custom_material_key_.value()](
          const CookedMaterialEntry& entry) { return entry.key == selected; });
      if (it == cooked_material_entries_.end()) {
        selected_custom_material_key_.reset();
        selection_cleared = true;
      }
    }
  } catch (const std::exception& e) {
    selection_cleared = selected_custom_material_key_.has_value();
    LOG_F(WARNING,
      "TexturedCube: failed to enumerate cooked materials from '{}' error='{}'",
      cooked_root.string(), e.what());
    selected_custom_material_key_.reset();
  }

  if (selection_cleared && use_custom_material_) {
    cube_needs_rebuild_ = true;
  }

  LOG_F(INFO, "TexturedCube: material refresh completed entries={}",
    cooked_material_entries_.size());
}

auto MaterialsSandboxVm::GetMetadataJson(uint32_t entry_index) const
  -> std::string
{
  if (!texture_service_) {
    return {};
  }
  const auto entry = std::ranges::find(
    cooked_entries_, entry_index, &CookedTextureEntry::index);
  return entry == cooked_entries_.end()
    ? std::string {}
    : texture_service_->GetTextureMetadataJson(entry->content_hash);
}

} // namespace oxygen::examples::textured_cube::ui
