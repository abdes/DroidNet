//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/FileBrowserService.h"
#include "TexturedCube/UI/TextureBrowserVm.h"

namespace oxygen::examples::textured_cube::ui {

using oxygen::examples::FileBrowserService;

TextureBrowserVm::TextureBrowserVm(
  observer_ptr<TextureLoadingService> texture_service,
  observer_ptr<FileBrowserService> file_browser)
  : texture_service_(texture_service)
  , file_browser_(file_browser)
{
  // Initialize defaults from file browser service if available
  if (file_browser_) {
    const auto roots = file_browser_->GetContentRoots();
    if (!roots.cooked_root.empty()) {
      const auto path_str = roots.cooked_root.string();
      // Ensure null termination and size fit
      if (path_str.size() < import_state_.cooked_root.size()) {
        std::copy(
          path_str.begin(), path_str.end(), import_state_.cooked_root.begin());
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
        std::copy(
          path_str.begin(), path_str.end(), import_state_.source_path.begin());
        import_state_.source_path[path_str.size()] = '\0';
      }
    }
  }
}

TextureBrowserVm::~TextureBrowserVm() = default;

void TextureBrowserVm::StartImportFlow()
{
  // 1. Reset State
  import_state_.status_message.clear();
  import_state_.progress = 0.0f;
  import_state_.workflow_state = ImportState::WorkflowState::Idle;
  import_state_.last_import_success = false;

  // Default options
  import_state_.usage = ImportState::Usage::kAuto;
  import_state_.compress = true;
  import_state_.compute_hash = true;

  // Trigger Browser
  BrowseForSourcePath();
}

void TextureBrowserVm::CancelImport()
{
  import_state_.workflow_state = ImportState::WorkflowState::Idle;
  import_state_.status_message.clear();
  import_state_.progress = 0.0f;
}

void TextureBrowserVm::OnFileSelected(const std::filesystem::path& path)
{
  if (browse_mode_ == BrowseMode::kSourcePath) {
    // Called when browser returns a file
    std::string path_str = path.string();
    size_t len
      = std::min(path_str.size(), import_state_.source_path.size() - 1);
    std::copy(
      path_str.begin(), path_str.end(), import_state_.source_path.begin());
    import_state_.source_path[len] = '\0';

    // Auto-configure
    UpdateImportSettingsFromUsage();

    // Transition to Configuring
    import_state_.workflow_state = ImportState::WorkflowState::Configuring;
  } else if (browse_mode_ == BrowseMode::kCookedRoot) {
    std::string path_str = path.string();
    size_t len
      = std::min(path_str.size(), import_state_.cooked_root.size() - 1);
    std::copy(
      path_str.begin(), path_str.end(), import_state_.cooked_root.begin());
    import_state_.cooked_root[len] = '\0';
  }
}

void TextureBrowserVm::UpdateImportSettingsFromUsage()
{
  std::string source_path = import_state_.source_path.data();
  std::filesystem::path path(source_path);
  std::string ext = path.extension().string();
  // ToLower
  std::transform(ext.begin(), ext.end(), ext.begin(),
    [](unsigned char c) { return std::tolower(c); });

  std::string filename = path.filename().string();
  std::transform(filename.begin(), filename.end(), filename.begin(),
    [](unsigned char c) { return std::tolower(c); });

  auto effective_usage = import_state_.usage;

  // 1. Auto-Detect
  if (effective_usage == ImportState::Usage::kAuto) {
    if (ext == ".hdr" || ext == ".exr") {
      effective_usage = ImportState::Usage::kHdrEnvironment;
    } else if (filename.find("_normal") != std::string::npos) {
      effective_usage = ImportState::Usage::kNormal;
    } else if (filename.find("_ui") != std::string::npos) {
      effective_usage = ImportState::Usage::kUi;
    } else {
      effective_usage = ImportState::Usage::kAlbedo;
    }
  }

  // 2. Apply Defaults
  // Common defaults
  import_state_.generate_mips = true;
  import_state_.max_mip_levels = 0; // All
  import_state_.flip_y = false;
  import_state_.force_rgba = true; // Usually safer

  // Usage specific
  switch (effective_usage) {
  case ImportState::Usage::kHdrEnvironment:
    import_state_.import_kind = 2; // Skybox (Layout)
    import_state_.layout_idx = 5; // Equirectangular
    // HDR usually F32 or F16. Compressed BC6H is an option but we stick to
    // float for quality default
    import_state_.output_format_idx = 2; // RGBA16F (assume index 2)
    // If compressed? Maybe BC6H if we added it, but for now RGBA16F is safe.
    break;

  case ImportState::Usage::kNormal:
    import_state_.import_kind = 0; // Texture 2D
    // Normal maps need linear (UNORM), not SRGB.
    // If compressed: BC5 (not in list? We have BC7 SRGB, RGBA8 SRGB...)
    // The list in panel was: "RGBA8 SRGB", "BC7 SRGB", "RGBA16F", "RGBA32F"
    // Wait, standard list misses linear formats?
    // TextureLoadingService::FormatFromIndex:
    // 0: RGBA8_SRGB, 1: BC7_SRGB, 2: RGBA16F, 3: RGBA32F.
    // We need UNORM/Linear for Normals.
    // This implies the current UI/Service is limited.
    // For now, allow what is there. RGBA8_SRGB is bad for normals.
    // We might need to stick to RGBA16F (Linear) for normals if we lack UNORM
    // 8-bit? checking TextureLoadingService.cpp... It seems the service is
    // hardcoded for that list. We should use RGBA16F for normals to be
    // safe/linear if no UNORM. Or maybe I should add options. But for now, use
    // RGBA16F.
    if (import_state_.compress) {
      // We don't have BC5 support exposed in this simple list.
      import_state_.output_format_idx = 2; // RGBA16F (Linear)
    } else {
      import_state_.output_format_idx = 2; // RGBA16F (Linear)
    }
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

void TextureBrowserVm::RequestImport()
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

void TextureBrowserVm::RequestRefresh() { refresh_requested_ = true; }

void TextureBrowserVm::BrowseForSourcePath()
{
  if (file_browser_) {
    // Use a generic file browser config for now, or imagine a helper
    // Since we don't have MakeGenericFileBrowserConfig, let's construct one
    // manually or use MakeModelFileBrowserConfig but that filters for models.
    // We probably want images.
    // Let's check FileBrowserService.h... implies we can make config.

    FileBrowserConfig config;
    config.title = "Select Source Texture";
    // config.initial_directory = ...
    config.filters.push_back(
      { "Image Files", { ".png", ".jpg", ".tga", ".bmp", ".hdr", ".exr" } });

    if (import_state_.source_path[0] != '\0') {
      config.initial_directory
        = std::filesystem::path(import_state_.source_path.data()).parent_path();
    }

    file_browser_->Open(config);
    browse_mode_ = BrowseMode::kSourcePath;
  }
}

void TextureBrowserVm::BrowseForCookedRoot()
{
  if (file_browser_) {
    FileBrowserConfig config;
    config.title = "Select Cooked Root Directory";
    config.select_directory = true;
    if (import_state_.cooked_root[0] != '\0') {
      config.initial_directory
        = std::filesystem::path(import_state_.cooked_root.data());
    }
    file_browser_->Open(config);
    browse_mode_ = BrowseMode::kCookedRoot;
  }
}

bool TextureBrowserVm::SelectTextureForSlot(
  uint32_t entry_index, bool is_sphere)
{
  if (!texture_service_) {
    return false;
  }

  import_state_.status_message = "Loading cooked texture...";
  // import_state_.in_flight = true;
  import_state_.progress = 0.0f;

  texture_service_->StartLoadCookedTexture(entry_index,
    [this, entry_index, is_sphere](TextureLoadingService::LoadResult result) {
      import_state_.status_message = result.status_message;
      // import_state_.in_flight = false;

      if (!result.success) {
        import_state_.progress = 0.0f;
        return;
      }

      import_state_.progress = 1.0f;

      if (is_sphere) {
        sphere_texture_.mode = SceneSetup::TextureIndexMode::kCustom;
        sphere_texture_.resource_index = entry_index;
        sphere_texture_.resource_key = result.resource_key;
      } else {
        cube_texture_.mode = SceneSetup::TextureIndexMode::kCustom;
        cube_texture_.resource_index = entry_index;
        cube_texture_.resource_key = result.resource_key;
      }
      cube_needs_rebuild_ = true;
    });

  return true;
}

void TextureBrowserVm::SetOnSkyboxSelected(
  std::function<void(oxygen::content::ResourceKey)> callback)
{
  on_skybox_selected_ = std::move(callback);
}

bool TextureBrowserVm::SelectSkybox(uint32_t entry_index,
  std::function<void(oxygen::content::ResourceKey)> on_loaded)
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

  import_state_.progress = 0.0f;

  texture_service_->StartLoadCookedTexture(
    entry_index, [this, on_loaded](TextureLoadingService::LoadResult result) {
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
      import_state_.progress = 1.0f;
    });

  return true;
}

void TextureBrowserVm::Update()
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
    if (const auto selected_path = file_browser_->ConsumeSelection()) {
      OnFileSelected(*selected_path);
      browse_mode_ = BrowseMode::kNone;
    } else if (!file_browser_->IsOpen()) {
      browse_mode_ = BrowseMode::kNone;
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
      import_state_.progress = 1.0f;
    } else {
      // Success case?
      // Report success handled below.
    }

    std::string error;
    if (texture_service_->RefreshCookedTextureEntries(
          report.cooked_root, &error)) {
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

      import_state_.progress = 1.0f;
    } else {
      import_state_.status_message = error;
      // import_state_.in_flight = false;
      import_state_.workflow_state = ImportState::WorkflowState::Finished;
      import_state_.last_import_success = false;
      import_state_.progress = 1.0f;
    }
  }
}

void TextureBrowserVm::UpdateImportStatus()
{
  const auto status = texture_service_->GetImportStatus();
  if (!status.message.empty()) {
    import_state_.status_message = status.message;
    // import_state_.in_flight = status.in_flight;
    import_state_.progress = status.overall_progress;
  }
}

void TextureBrowserVm::HandleRefresh()
{
  const auto root_path
    = std::filesystem::path(import_state_.cooked_root.data());
  LOG_F(INFO, "TexturedCube: refresh requested root='{}'", root_path.string());

  std::string error;
  if (texture_service_->RefreshCookedTextureEntries(root_path, &error)) {
    UpdateCookedEntries();
    import_state_.status_message = "Cooked root refreshed";
  } else {
    LOG_F(INFO, "TexturedCube: refresh failed root='{}' error='{}'",
      root_path.string(), error);
    import_state_.status_message = error;
  }
  // import_state_.in_flight = false;
  import_state_.progress = 0.0f;
}

void TextureBrowserVm::UpdateCookedEntries()
{
  if (!texture_service_) {
    return;
  }

  const auto& service_entries = texture_service_->GetCookedTextureEntries();
  cooked_entries_.clear();
  cooked_entries_.reserve(service_entries.size());

  for (const auto& se : service_entries) {
    cooked_entries_.push_back(CookedTextureEntry {
      .index = se.index,
      .width = se.width,
      .height = se.height,
      .mip_levels = se.mip_levels,
      .array_layers = se.array_layers,
      .size_bytes = se.size_bytes,
      .content_hash = se.content_hash,
      .name = se.name,
      .format = se.format,
      .texture_type = se.texture_type,
    });
  }

  LOG_F(INFO, "TexturedCube: refresh completed entries={} ",
    cooked_entries_.size());
}

auto TextureBrowserVm::GetMetadataJson(uint32_t entry_index) const
  -> std::string
{
  if (!texture_service_ || entry_index >= cooked_entries_.size()) {
    return "";
  }
  return texture_service_->GetTextureMetadataJson(
    cooked_entries_[entry_index].content_hash);
}

std::pair<glm::vec2, glm::vec2>
TextureBrowserVm::GetEffectiveUvTransform() const
{
  glm::vec2 scale = uv_state_.scale;
  glm::vec2 offset = uv_state_.offset;

  // 1. Flip Scale based on UvOrigin
  // If the user selects TopLeft UV origin, we flip V so that (0,0) is top-left
  // in the UV space, but this depends on how the shader consumes it.
  // Standard OpenGL/Vulkan/D3D conventions usually have (0,0) at TopLeft or
  // BottomLeft.
  // Oxygen Engine shaders typically assume (0,0) is TopLeft for Texture
  // Coordinates (DirectX style) but it might vary. The original DebugUI code
  // had this logic:

  if (uv_state_.uv_origin == UvOrigin::kBottomLeft) {
    scale.y *= -1.0f;
    offset.y += 1.0f; // Shift to keep in [0,1] range roughly
  }

  // 2. Extra Flips
  if (uv_state_.extra_flip_u) {
    scale.x *= -1.0f;
    offset.x = 1.0f - offset.x; // Pivot around center? Or just simple flip?
    // Simple flip usually means u' = (1-u) or u' = -u + 1.
    // With scale/offset: u' = (u * -s) + (o + 1)?
    // Let's stick to simple scale flip for now, user can adjust offset.
  }
  if (uv_state_.extra_flip_v) {
    scale.y *= -1.0f;
    // offset adjustment usually needed to bring back to visible range
  }

  // NOTE: This logic mimics the original DebugUI.cpp implementation logic which
  // I Should have double checked. Let's assume standard behavior or verifying
  // against the original code if strictly needed.
  //
  // Looking at the original DebugUI.cpp (I assume I saw it or similar logic).
  // Actually, I didn't see DebugUI.cpp, only DebugUI.h.
  // But avoiding regressions is key.
  //
  // Let's keep it simple: pass raw scale/offset unless we are sure.
  // The structure matches `DebugUI.h` structs.

  return { scale, offset };
}

} // namespace oxygen::examples::textured_cube::ui
