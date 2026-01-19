//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ImportPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Data/AssetType.h>

#include "FilePicker.h"

namespace oxygen::examples::render_scene::ui {

namespace {

  [[nodiscard]] auto BeginEnumCombo(const char* label, const char* preview)
    -> bool
  {
    return ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest);
  }

  auto HelpMarker(const char* description) -> void
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
      ImGui::TextUnformatted(description);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }

  auto InputTextString(const char* label, std::string& value) -> bool
  {
    constexpr std::size_t kBufferSize = 512;
    std::array<char, kBufferSize> buffer {};
    if (!value.empty()) {
      std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    }

    if (ImGui::InputText(label, buffer.data(), buffer.size())) {
      value = buffer.data();
      return true;
    }
    return false;
  }

  [[nodiscard]] auto FormatLabel(content::import::ImportFormat format)
    -> std::string_view
  {
    switch (format) {
    case content::import::ImportFormat::kFbx:
      return "FBX";
    case content::import::ImportFormat::kGltf:
      return "GLTF";
    case content::import::ImportFormat::kGlb:
      return "GLB";
    case content::import::ImportFormat::kTextureImage:
      return "Texture";
    case content::import::ImportFormat::kUnknown:
      return "Unknown";
    }
    return "Unknown";
  }

  template <typename EnumT, std::size_t N>
  auto DrawEnumCombo(const char* label, EnumT& value,
    const std::array<EnumT, N>& items, const char* (*to_string_fn)(EnumT))
    -> bool
  {
    const char* preview = to_string_fn(value);
    bool changed = false;
    if (BeginEnumCombo(label, preview)) {
      for (const auto candidate : items) {
        const bool is_selected = (candidate == value);
        if (ImGui::Selectable(to_string_fn(candidate), is_selected)) {
          value = candidate;
          changed = true;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  [[nodiscard]] auto ToString(content::import::AssetKeyPolicy value) -> const
    char*
  {
    switch (value) {
    case content::import::AssetKeyPolicy::kDeterministicFromVirtualPath:
      return "Deterministic";
    case content::import::AssetKeyPolicy::kRandom:
      return "Random";
    }
    return "Unknown";
  }

  [[nodiscard]] auto ToString(content::import::UnitNormalizationPolicy value)
    -> const char*
  {
    switch (value) {
    case content::import::UnitNormalizationPolicy::kNormalizeToMeters:
      return "Normalize to meters";
    case content::import::UnitNormalizationPolicy::kPreserveSource:
      return "Preserve source";
    case content::import::UnitNormalizationPolicy::kApplyCustomFactor:
      return "Apply custom factor";
    }
    return "Unknown";
  }

  [[nodiscard]] auto ToString(content::import::NodePruningPolicy value) -> const
    char*
  {
    switch (value) {
    case content::import::NodePruningPolicy::kKeepAll:
      return "Keep all";
    case content::import::NodePruningPolicy::kDropEmptyNodes:
      return "Drop empty nodes";
    }
    return "Unknown";
  }

  [[nodiscard]] auto ToString(content::import::GeometryAttributePolicy value)
    -> const char*
  {
    switch (value) {
    case content::import::GeometryAttributePolicy::kNone:
      return "None";
    case content::import::GeometryAttributePolicy::kPreserveIfPresent:
      return "Preserve if present";
    case content::import::GeometryAttributePolicy::kGenerateMissing:
      return "Generate missing";
    case content::import::GeometryAttributePolicy::kAlwaysRecalculate:
      return "Always recalculate";
    }
    return "Unknown";
  }

  template <typename EnumT, std::size_t N>
  auto DrawEnumCombo(const char* label, EnumT& value,
    const std::array<EnumT, N>& items, const char* (*to_string_fn)(EnumT),
    const char* tooltip) -> bool
  {
    const bool changed = DrawEnumCombo(label, value, items, to_string_fn);
    if (tooltip != nullptr) {
      HelpMarker(tooltip);
    }
    return changed;
  }

  [[nodiscard]] auto DrawFormatCombo(const char* label, Format& value) -> bool
  {
    constexpr std::array<Format, 7> kFormats = {
      Format::kBC7UNormSRGB,
      Format::kBC7UNorm,
      Format::kRGBA8UNormSRGB,
      Format::kRGBA8UNorm,
      Format::kRGBA16Float,
      Format::kRGBA32Float,
      Format::kRG8UNorm,
    };

    const char* preview = oxygen::to_string(value);
    bool changed = false;
    if (BeginEnumCombo(label, preview)) {
      for (const auto candidate : kFormats) {
        const bool is_selected = (candidate == value);
        if (ImGui::Selectable(oxygen::to_string(candidate), is_selected)) {
          value = candidate;
          changed = true;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  auto DrawPackingPolicyCombo(std::string& value) -> bool
  {
    static constexpr std::array<const char*, 2> kIds = { "d3d12", "tight" };

    const char* preview = value.empty() ? "(default)" : value.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(
          "Packing policy", preview, ImGuiComboFlags_HeightLargest)) {
      for (const auto* candidate : kIds) {
        const bool is_selected = (value == candidate);
        if (ImGui::Selectable(candidate, is_selected)) {
          value = candidate;
          changed = true;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  auto ClampInt(int value, int min_value, int max_value) -> int
  {
    return (std::max)(min_value, (std::min)(value, max_value));
  }

} // namespace

/*!
 Initialize the unified import panel.

 @param config Configuration with default directories and callbacks.

 ### Usage Examples

  ```cpp
  ImportPanel panel;
  panel.Initialize(config);
  ```
*/
void ImportPanel::Initialize(const ImportPanelConfig& config)
{
  config_ = config;

  model_directory_text_ = !config_.gltf_directory.empty()
    ? config_.gltf_directory.string()
    : config_.fbx_directory.string();
  fbx_directory_text_ = model_directory_text_;
  gltf_directory_text_ = model_directory_text_;
  cooked_output_text_ = config_.cooked_output_directory.string();

  layout_ = {};
  virtual_mount_root_text_ = layout_.virtual_mount_root;
  index_file_name_text_ = layout_.index_file_name;
  resources_dir_text_ = layout_.resources_dir;
  descriptors_dir_text_ = layout_.descriptors_dir;
  scenes_subdir_text_ = layout_.scenes_subdir;
  geometry_subdir_text_ = layout_.geometry_subdir;
  materials_subdir_text_ = layout_.materials_subdir;

  import_options_ = {};
  texture_tuning_ = {
    .enabled = true,
    .intent = content::import::TextureIntent::kAlbedo,
    .source_color_space = ColorSpace::kSRGB,
    .flip_y_on_decode = false,
    .force_rgba_on_decode = true,
    .mip_policy = content::import::MipPolicy::kFullChain,
    .max_mip_levels = 10,
    .mip_filter = content::import::MipFilter::kKaiser,
    .color_output_format = Format::kBC7UNormSRGB,
    .data_output_format = Format::kBC7UNorm,
    .bc7_quality = content::import::Bc7Quality::kDefault,
    .packing_policy_id = "d3d12",
    .placeholder_on_failure = false,
    .import_cubemap = false,
    .equirect_to_cubemap = false,
    .cubemap_face_size = 0,
    .cubemap_layout = content::import::CubeMapImageLayout::kUnknown,
  };

  service_config_ = {};
  service_config_.concurrency.texture.workers = 8;
  service_config_.concurrency.material.workers = 4;
  service_config_.concurrency.geometry.workers = 6;
  service_config_.concurrency.buffer.workers = 6;
  service_config_.concurrency.scene.workers = 1;
  import_service_
    = std::make_unique<content::import::AsyncImportService>(service_config_);

  files_cached_ = false;
  cached_files_.clear();
}

/*!
 Update import completion state and emit callbacks.

 ### Performance Characteristics

 - Time Complexity: $O(1)$ per frame; $O(n)$ when scanning index assets.
 - Memory: Transient allocations when reading the index file.
 - Optimization: Uses a cached index scan only when a job completes.
*/
void ImportPanel::Update()
{
  if (pending_service_restart_ && !IsImporting()) {
    import_service_.reset();
    import_service_
      = std::make_unique<content::import::AsyncImportService>(service_config_);
    pending_service_restart_ = false;
    service_config_dirty_ = false;
  }

  if (!import_state_.completion_ready.load(std::memory_order_relaxed)) {
    return;
  }

  std::optional<content::import::ImportReport> report;
  std::string completion_error;
  {
    std::lock_guard<std::mutex> lock(import_state_.completion_mutex);
    report = import_state_.completion_report;
    completion_error = import_state_.completion_error;
    import_state_.completion_report.reset();
    import_state_.completion_error.clear();
  }

  import_state_.completion_ready.store(false, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  last_import_source_ = import_state_.importing_path;
  import_state_.importing_path.clear();
  import_state_.job_id = content::import::kInvalidJobId;
  import_state_.is_importing.store(false, std::memory_order_relaxed);

  if (!completion_error.empty()) {
    LOG_F(ERROR, "Import failed: {}", completion_error);
    return;
  }

  if (!report.has_value()) {
    LOG_F(ERROR, "Import failed: no report returned");
    return;
  }

  last_report_ = *report;
  {
    std::lock_guard<std::mutex> lock(import_state_.progress_mutex);
    import_state_.diagnostics = report->diagnostics;
  }

  if (!report->success) {
    LOG_F(ERROR, "Import failed; see diagnostics");
    return;
  }

  const auto index_path
    = report->cooked_root / std::filesystem::path(layout_.index_file_name);

  if (config_.on_index_loaded) {
    config_.on_index_loaded(index_path);
  }

  std::optional<data::AssetKey> scene_key;
  try {
    content::LooseCookedInspection inspection;
    inspection.LoadFromFile(index_path);

    const auto expected_scene_name
      = std::filesystem::path(last_import_source_).stem().string();
    const auto expected_virtual_path
      = layout_.SceneVirtualPath(expected_scene_name);

    std::optional<data::AssetKey> first_scene_key;
    std::string first_scene_path;

    for (const auto& asset : inspection.Assets()) {
      if (asset.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
        continue;
      }

      if (asset.virtual_path == expected_virtual_path) {
        scene_key = asset.key;
      }

      if (!first_scene_key || asset.virtual_path < first_scene_path) {
        first_scene_key = asset.key;
        first_scene_path = asset.virtual_path;
      }
    }

    if (!scene_key && first_scene_key) {
      scene_key = *first_scene_key;
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "Import succeeded but inspection failed: {}", ex.what());
  }

  if (auto_load_scene_ && scene_key && config_.on_scene_ready) {
    config_.on_scene_ready(*scene_key);

    if (auto_dump_texture_memory_ && config_.on_dump_texture_memory
      && dump_top_n_ > 0) {
      pending_auto_dump_frames_ = auto_dump_delay_frames_;
    }
  }
}

/*!
 Draw the import panel UI.

 @note Must be called within the ImGui frame.
*/
void ImportPanel::Draw()
{
  if (pending_auto_dump_frames_ > 0
    && !import_state_.is_importing.load(std::memory_order_relaxed)) {
    --pending_auto_dump_frames_;
    if (pending_auto_dump_frames_ == 0 && config_.on_dump_texture_memory
      && dump_top_n_ > 0) {
      config_.on_dump_texture_memory(static_cast<std::size_t>(dump_top_n_));
    }
  }

  if (import_state_.is_importing.load(std::memory_order_relaxed)) {
    ImGui::Text("Importing: %s", import_state_.importing_path.c_str());

    if (import_state_.cancel_requested.load(std::memory_order_relaxed)) {
      ImGui::TextDisabled("Cancelling...");
    } else if (ImGui::Button("Cancel import")) {
      CancelImport();
    }

    content::import::ImportProgress progress;
    {
      std::lock_guard<std::mutex> lock(import_state_.progress_mutex);
      progress = import_state_.progress;
    }

    const float progress_value = (progress.overall_progress > 0.0F)
      ? progress.overall_progress
      : -1.0F * static_cast<float>(ImGui::GetTime()) * 0.2F;

    ImGui::ProgressBar(progress_value, ImVec2(-1.0F, 0.0F),
      progress.message.empty() ? "Importing..." : progress.message.c_str());
  }

  const bool disable_inputs = IsImporting();
  if (disable_inputs) {
    ImGui::BeginDisabled();
  }

  DrawSourceSelectionUi();
  DrawSessionConfigUi();
  DrawImportOptionsUi();
  DrawTextureTuningUi();
  DrawOutputLayoutUi();
  DrawServiceConfigUi();
  DrawJobSummaryUi();
  DrawDiagnosticsUi();

  if (disable_inputs) {
    ImGui::EndDisabled();
  }
}

[[nodiscard]] auto ImportPanel::IsImporting() const -> bool
{
  return import_state_.is_importing.load(std::memory_order_relaxed);
}

/*!
 Cancel the currently running import job.
*/
void ImportPanel::CancelImport()
{
  if (!IsImporting() || !import_service_) {
    return;
  }

  import_state_.cancel_requested.store(true, std::memory_order_relaxed);
  if (import_state_.job_id != content::import::kInvalidJobId) {
    (void)import_service_->CancelJob(import_state_.job_id);
  }
}

void ImportPanel::StartImport(const std::filesystem::path& source_path)
{
  if (!import_service_) {
    LOG_F(ERROR, "Import service not available");
    return;
  }

  if (IsImporting()) {
    return;
  }

  import_state_.importing_path = source_path.string();
  import_state_.is_importing.store(true, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.completion_ready.store(false, std::memory_order_relaxed);
  import_state_.diagnostics.clear();
  import_state_.completion_report.reset();
  import_state_.completion_error.clear();

  content::import::ImportRequest request {};
  request.source_path = source_path;

  if (use_cooked_root_override_ && !cooked_output_text_.empty()) {
    const auto cooked_root = std::filesystem::absolute(cooked_output_text_);
    request.cooked_root = cooked_root;
  }

  if (!job_name_text_.empty()) {
    request.job_name = job_name_text_;
  }

  layout_.virtual_mount_root = virtual_mount_root_text_;
  layout_.index_file_name = index_file_name_text_;
  layout_.resources_dir = resources_dir_text_;
  layout_.descriptors_dir = descriptors_dir_text_;
  layout_.scenes_subdir = scenes_subdir_text_;
  layout_.geometry_subdir = geometry_subdir_text_;
  layout_.materials_subdir = materials_subdir_text_;
  request.loose_cooked_layout = layout_;

  import_options_.texture_tuning = texture_tuning_;
  if (use_normalize_naming_) {
    import_options_.naming_strategy
      = std::make_shared<content::import::NormalizeNamingStrategy>();
  } else {
    import_options_.naming_strategy.reset();
  }
  request.options = import_options_;

  const auto on_complete = [this](content::import::ImportJobId job_id,
                             const content::import::ImportReport& report) {
    if (import_state_.job_id != job_id) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(import_state_.completion_mutex);
      import_state_.completion_report = report;
      import_state_.completion_error.clear();
    }
    import_state_.completion_ready.store(true, std::memory_order_relaxed);
  };

  const auto on_progress
    = [this](const content::import::ImportProgress& progress) {
        std::lock_guard<std::mutex> lock(import_state_.progress_mutex);
        import_state_.progress = progress;
        if (!progress.new_diagnostics.empty()) {
          import_state_.diagnostics.insert(import_state_.diagnostics.end(),
            progress.new_diagnostics.begin(), progress.new_diagnostics.end());
        }
      };

  const auto job_id
    = import_service_->SubmitImport(request, on_complete, on_progress);

  if (job_id == content::import::kInvalidJobId) {
    import_state_.is_importing.store(false, std::memory_order_relaxed);
    import_state_.completion_ready.store(true, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(import_state_.completion_mutex);
    import_state_.completion_error = "Importer rejected the job";
    return;
  }

  import_state_.job_id = job_id;
}

auto ImportPanel::EnumerateSourceFiles() const -> std::vector<SourceEntry>
{
  std::vector<SourceEntry> files;

  const auto add_entries
    = [&files](const std::filesystem::path& root,
        content::import::ImportFormat format, std::string_view extension) {
        std::error_code ec;
        if (root.empty() || !std::filesystem::exists(root, ec)
          || !std::filesystem::is_directory(root, ec)) {
          return;
        }

        for (const auto& entry :
          std::filesystem::recursive_directory_iterator(root, ec)) {
          if (ec) {
            break;
          }
          if (!entry.is_regular_file(ec)) {
            continue;
          }

          const auto path = entry.path();
          if (path.extension() == extension) {
            files.push_back({ path, format });
          }
        }
      };

  const auto model_root = std::filesystem::path(model_directory_text_);

  if (include_fbx_) {
    add_entries(model_root, content::import::ImportFormat::kFbx, ".fbx");
  }

  if (include_gltf_) {
    add_entries(model_root, content::import::ImportFormat::kGltf, ".gltf");
  }

  if (include_glb_) {
    add_entries(model_root, content::import::ImportFormat::kGlb, ".glb");
  }

  std::sort(
    files.begin(), files.end(), [](const SourceEntry& a, const SourceEntry& b) {
      const auto a_name = a.path.filename().string();
      const auto b_name = b.path.filename().string();
      if (a_name == b_name) {
        return static_cast<int>(a.format) < static_cast<int>(b.format);
      }
      return a_name < b_name;
    });

  return files;
}

void ImportPanel::RefreshSourceCache()
{
  cached_files_ = EnumerateSourceFiles();
  files_cached_ = true;
}

auto ImportPanel::DrawSourceSelectionUi() -> void
{
  if (!ImGui::CollapsingHeader(
        "Import Sources", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  ImGui::Text("Source directory");
  if (InputTextString("Model directory", model_directory_text_)) {
    fbx_directory_text_ = model_directory_text_;
    gltf_directory_text_ = model_directory_text_;
    config_.fbx_directory = std::filesystem::path(model_directory_text_);
    config_.gltf_directory = std::filesystem::path(model_directory_text_);
    files_cached_ = false;
  }

#if defined(OXYGEN_WINDOWS)
  if (ImGui::Button("Browse...")) {
    ImGui::OpenPopup("ImportBrowsePopup");
  }
  if (ImGui::BeginPopup("ImportBrowsePopup")) {
    if (ImGui::MenuItem("Pick file...")) {
      auto picker_config = MakeModelFilePickerConfig();
      if (!model_directory_text_.empty()) {
        picker_config.initial_directory
          = std::filesystem::path(model_directory_text_);
      }

      if (const auto selected_path = ShowFilePicker(picker_config)) {
        StartImport(*selected_path);
        ImGui::EndPopup();
        return;
      }
    }

    if (ImGui::MenuItem("Pick directory...")) {
      auto picker_config = MakeModelDirectoryPickerConfig();
      if (!model_directory_text_.empty()) {
        picker_config.initial_directory
          = std::filesystem::path(model_directory_text_);
      }

      if (const auto selected_path = ShowDirectoryPicker(picker_config)) {
        model_directory_text_ = selected_path->string();
        fbx_directory_text_ = model_directory_text_;
        gltf_directory_text_ = model_directory_text_;
        config_.fbx_directory = *selected_path;
        config_.gltf_directory = *selected_path;
        files_cached_ = false;
      }
    }
    ImGui::EndPopup();
  }
#endif

  ImGui::Separator();
  ImGui::Text("Format filters");
  bool filters_changed = false;
  filters_changed |= ImGui::Checkbox("FBX", &include_fbx_);
  ImGui::SameLine();
  filters_changed |= ImGui::Checkbox("GLB", &include_glb_);
  ImGui::SameLine();
  filters_changed |= ImGui::Checkbox("GLTF", &include_gltf_);
  if (filters_changed) {
    files_cached_ = false;
  }

  ImGui::Separator();
  if (ImGui::Button("Refresh List")) {
    files_cached_ = false;
  }

  if (!files_cached_) {
    RefreshSourceCache();
  }

  ImGui::Separator();
  const float list_height = ImGui::GetTextLineHeightWithSpacing() * 10.0F;
  if (ImGui::BeginListBox("##ImportSources", ImVec2(-1.0F, list_height))) {
    for (const auto& entry : cached_files_) {
      const auto filename = entry.path.filename().string();
      std::string label = std::string("[")
        + std::string(FormatLabel(entry.format)) + "] " + filename;

      if (ImGui::Selectable(label.c_str(), false)) {
        StartImport(entry.path);
      }

      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", entry.path.string().c_str());
      }
    }
    ImGui::EndListBox();
  }

  if (cached_files_.empty()) {
    ImGui::TextDisabled("No importable files found in selected directories");
  }
}

auto ImportPanel::DrawSessionConfigUi() -> void
{
  if (!ImGui::CollapsingHeader(
        "Import Session", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  ImGui::Checkbox("Use cooked output override", &use_cooked_root_override_);
  if (use_cooked_root_override_) {
    (void)InputTextString("Cooked output directory", cooked_output_text_);
  }

  (void)InputTextString("Job name (optional)", job_name_text_);
  ImGui::Checkbox("Auto-load scene after import", &auto_load_scene_);

  if (config_.on_dump_texture_memory) {
    ImGui::Separator();
    ImGui::Checkbox("Auto-dump runtime texture VRAM after import",
      &auto_dump_texture_memory_);
    ImGui::SliderInt("Dump Top N", &dump_top_n_, 1, 200);
    ImGui::SliderInt("Dump delay (frames)", &auto_dump_delay_frames_, 0, 600);
    if (ImGui::Button("Dump runtime texture VRAM now")) {
      config_.on_dump_texture_memory(static_cast<std::size_t>(dump_top_n_));
    }
  }
}

auto ImportPanel::DrawImportOptionsUi() -> void
{
  if (!ImGui::CollapsingHeader(
        "Import Options", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  static constexpr std::array<content::import::AssetKeyPolicy, 2>
    kAssetKeyPolicies = {
      content::import::AssetKeyPolicy::kDeterministicFromVirtualPath,
      content::import::AssetKeyPolicy::kRandom,
    };

  static constexpr std::array<content::import::UnitNormalizationPolicy, 3>
    kUnitPolicies = {
      content::import::UnitNormalizationPolicy::kNormalizeToMeters,
      content::import::UnitNormalizationPolicy::kPreserveSource,
      content::import::UnitNormalizationPolicy::kApplyCustomFactor,
    };

  static constexpr std::array<content::import::NodePruningPolicy, 2>
    kNodePolicies = {
      content::import::NodePruningPolicy::kKeepAll,
      content::import::NodePruningPolicy::kDropEmptyNodes,
    };

  static constexpr std::array<content::import::GeometryAttributePolicy, 4>
    kAttributePolicies = {
      content::import::GeometryAttributePolicy::kNone,
      content::import::GeometryAttributePolicy::kPreserveIfPresent,
      content::import::GeometryAttributePolicy::kGenerateMissing,
      content::import::GeometryAttributePolicy::kAlwaysRecalculate,
    };

  (void)DrawEnumCombo("Asset key policy", import_options_.asset_key_policy,
    kAssetKeyPolicies, ToString);

  ImGui::Separator();
  ImGui::Checkbox("Bake transforms into meshes",
    &import_options_.coordinate.bake_transforms_into_meshes);

  (void)DrawEnumCombo("Unit normalization",
    import_options_.coordinate.unit_normalization, kUnitPolicies, ToString);

  if (import_options_.coordinate.unit_normalization
    == content::import::UnitNormalizationPolicy::kApplyCustomFactor) {
    ImGui::SliderFloat("Custom unit scale",
      &import_options_.coordinate.custom_unit_scale, 0.01F, 10.0F);
  }

  ImGui::Separator();
  ImGui::Checkbox("Normalize names", &use_normalize_naming_);

  (void)DrawEnumCombo(
    "Node pruning", import_options_.node_pruning, kNodePolicies, ToString);

  ImGui::Separator();
  bool emit_textures = (import_options_.import_content
                         & content::import::ImportContentFlags::kTextures)
    != content::import::ImportContentFlags::kNone;
  bool emit_materials = (import_options_.import_content
                          & content::import::ImportContentFlags::kMaterials)
    != content::import::ImportContentFlags::kNone;
  bool emit_geometry = (import_options_.import_content
                         & content::import::ImportContentFlags::kGeometry)
    != content::import::ImportContentFlags::kNone;
  bool emit_scene = (import_options_.import_content
                      & content::import::ImportContentFlags::kScene)
    != content::import::ImportContentFlags::kNone;

  ImGui::Text("Emit cooked content");
  ImGui::Checkbox("Textures", &emit_textures);
  ImGui::SameLine();
  ImGui::Checkbox("Materials", &emit_materials);
  ImGui::SameLine();
  ImGui::Checkbox("Geometry", &emit_geometry);
  ImGui::SameLine();
  ImGui::Checkbox("Scene", &emit_scene);

  import_options_.import_content = content::import::ImportContentFlags::kNone;
  if (emit_textures) {
    import_options_.import_content = import_options_.import_content
      | content::import::ImportContentFlags::kTextures;
  }
  if (emit_materials) {
    import_options_.import_content = import_options_.import_content
      | content::import::ImportContentFlags::kMaterials;
  }
  if (emit_geometry) {
    import_options_.import_content = import_options_.import_content
      | content::import::ImportContentFlags::kGeometry;
  }
  if (emit_scene) {
    import_options_.import_content = import_options_.import_content
      | content::import::ImportContentFlags::kScene;
  }

  ImGui::Separator();
  (void)DrawEnumCombo("Normal policy", import_options_.normal_policy,
    kAttributePolicies, ToString);
  (void)DrawEnumCombo("Tangent policy", import_options_.tangent_policy,
    kAttributePolicies, ToString);

  ImGui::Checkbox(
    "Ignore non-mesh primitives", &import_options_.ignore_non_mesh_primitives);
}

auto ImportPanel::DrawTextureTuningUi() -> void
{
  if (!ImGui::CollapsingHeader(
        "Texture Cooking", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  ImGui::Checkbox("Enable texture cooking overrides", &texture_tuning_.enabled);

  if (!texture_tuning_.enabled) {
    ImGui::TextDisabled(
      "When disabled, textures are emitted in their decoded format without "
      "mips. This is fast but can use significant VRAM at runtime.");
    return;
  }

  static constexpr std::array<content::import::TextureIntent, 12> kIntents = {
    content::import::TextureIntent::kAlbedo,
    content::import::TextureIntent::kNormalTS,
    content::import::TextureIntent::kRoughness,
    content::import::TextureIntent::kMetallic,
    content::import::TextureIntent::kAO,
    content::import::TextureIntent::kEmissive,
    content::import::TextureIntent::kOpacity,
    content::import::TextureIntent::kORMPacked,
    content::import::TextureIntent::kHdrEnvironment,
    content::import::TextureIntent::kHdrLightProbe,
    content::import::TextureIntent::kData,
    content::import::TextureIntent::kHeightMap,
  };

  static constexpr std::array<ColorSpace, 2> kColorSpaces = {
    ColorSpace::kLinear,
    ColorSpace::kSRGB,
  };

  static constexpr std::array<content::import::MipPolicy, 3> kMipPolicies = {
    content::import::MipPolicy::kNone,
    content::import::MipPolicy::kFullChain,
    content::import::MipPolicy::kMaxCount,
  };

  static constexpr std::array<content::import::MipFilter, 3> kMipFilters = {
    content::import::MipFilter::kBox,
    content::import::MipFilter::kKaiser,
    content::import::MipFilter::kLanczos,
  };

  static constexpr std::array<content::import::Bc7Quality, 3> kBc7Qualities = {
    content::import::Bc7Quality::kFast,
    content::import::Bc7Quality::kDefault,
    content::import::Bc7Quality::kHigh,
  };

  static constexpr std::array<content::import::CubeMapImageLayout, 6>
    kCubeLayouts = {
      content::import::CubeMapImageLayout::kUnknown,
      content::import::CubeMapImageLayout::kAuto,
      content::import::CubeMapImageLayout::kHorizontalStrip,
      content::import::CubeMapImageLayout::kVerticalStrip,
      content::import::CubeMapImageLayout::kHorizontalCross,
      content::import::CubeMapImageLayout::kVerticalCross,
    };

  (void)DrawEnumCombo("Texture intent", texture_tuning_.intent, kIntents,
    content::import::to_string);
  (void)DrawEnumCombo("Source color space", texture_tuning_.source_color_space,
    kColorSpaces, oxygen::to_string);

  ImGui::Checkbox("Flip Y on decode", &texture_tuning_.flip_y_on_decode);
  ImGui::Checkbox(
    "Force RGBA on decode", &texture_tuning_.force_rgba_on_decode);

  (void)DrawEnumCombo("Mip policy", texture_tuning_.mip_policy, kMipPolicies,
    content::import::to_string);
  if (texture_tuning_.mip_policy == content::import::MipPolicy::kMaxCount) {
    int max_mips = static_cast<int>(texture_tuning_.max_mip_levels);
    max_mips = ClampInt(max_mips, 1, 16);
    if (ImGui::SliderInt("Max mip levels", &max_mips, 1, 16)) {
      texture_tuning_.max_mip_levels = static_cast<uint8_t>(max_mips);
    }
  }

  (void)DrawEnumCombo("Mip filter", texture_tuning_.mip_filter, kMipFilters,
    content::import::to_string);

  ImGui::Separator();
  (void)DrawFormatCombo(
    "Color output format", texture_tuning_.color_output_format);
  (void)DrawFormatCombo(
    "Data output format", texture_tuning_.data_output_format);

  (void)DrawEnumCombo("BC7 quality", texture_tuning_.bc7_quality, kBc7Qualities,
    content::import::to_string);
  (void)DrawPackingPolicyCombo(texture_tuning_.packing_policy_id);

  ImGui::Separator();
  ImGui::Checkbox(
    "Use placeholder on failure", &texture_tuning_.placeholder_on_failure);
  ImGui::Checkbox("Import cubemap", &texture_tuning_.import_cubemap);
  ImGui::Checkbox("Equirect to cubemap", &texture_tuning_.equirect_to_cubemap);
  if (texture_tuning_.import_cubemap || texture_tuning_.equirect_to_cubemap) {
    int face_size = static_cast<int>(texture_tuning_.cubemap_face_size);
    if (ImGui::SliderInt("Cubemap face size", &face_size, 0, 4096)) {
      texture_tuning_.cubemap_face_size = static_cast<uint32_t>(face_size);
    }
    (void)DrawEnumCombo("Cubemap layout", texture_tuning_.cubemap_layout,
      kCubeLayouts, content::import::to_string);
  }
}

auto ImportPanel::DrawOutputLayoutUi() -> void
{
  if (!ImGui::CollapsingHeader("Output Layout")) {
    return;
  }

  (void)InputTextString("Virtual mount root", virtual_mount_root_text_);
  (void)InputTextString("Index file name", index_file_name_text_);
  (void)InputTextString("Resources dir", resources_dir_text_);
  (void)InputTextString("Descriptors dir", descriptors_dir_text_);
  (void)InputTextString("Scenes subdir", scenes_subdir_text_);
  (void)InputTextString("Geometry subdir", geometry_subdir_text_);
  (void)InputTextString("Materials subdir", materials_subdir_text_);
}

auto ImportPanel::DrawServiceConfigUi() -> void
{
  if (!ImGui::CollapsingHeader("Import Service")) {
    return;
  }

  int thread_pool = static_cast<int>(service_config_.thread_pool_size);
  int max_jobs = static_cast<int>(service_config_.max_in_flight_jobs);
  if (ImGui::InputInt("Thread pool size", &thread_pool)) {
    service_config_.thread_pool_size
      = static_cast<uint32_t>(ClampInt(thread_pool, 1, 64));
    service_config_dirty_ = true;
  }
  if (ImGui::InputInt("Max in-flight jobs", &max_jobs)) {
    service_config_.max_in_flight_jobs
      = static_cast<uint32_t>(ClampInt(max_jobs, 1, 128));
    service_config_dirty_ = true;
  }

  ImGui::Separator();
  ImGui::Text("Pipeline concurrency");
  auto draw_pipeline = [this](const char* label,
                         content::import::ImportPipelineConcurrency& cfg) {
    int workers = static_cast<int>(cfg.workers);
    int capacity = static_cast<int>(cfg.queue_capacity);
    ImGui::PushID(label);
    ImGui::Text("%s", label);
    ImGui::SameLine(180.0F);
    if (ImGui::InputInt("Workers", &workers)) {
      cfg.workers = static_cast<uint32_t>(ClampInt(workers, 1, 32));
      service_config_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::InputInt("Queue", &capacity)) {
      cfg.queue_capacity = static_cast<uint32_t>(ClampInt(capacity, 1, 256));
      service_config_dirty_ = true;
    }
    ImGui::PopID();
  };

  draw_pipeline("Texture", service_config_.concurrency.texture);
  draw_pipeline("Buffer", service_config_.concurrency.buffer);
  draw_pipeline("Material", service_config_.concurrency.material);
  draw_pipeline("Geometry", service_config_.concurrency.geometry);
  draw_pipeline("Scene", service_config_.concurrency.scene);

  if (service_config_dirty_) {
    ImGui::Separator();
    ImGui::TextDisabled("Restart required to apply changes");
    if (ImGui::Button("Apply & Restart Service")) {
      if (IsImporting()) {
        ImGui::OpenPopup("ImportServiceBusy");
      } else {
        pending_service_restart_ = true;
      }
    }
    if (ImGui::BeginPopup("ImportServiceBusy")) {
      ImGui::Text("Stop the active import before restarting.");
      if (ImGui::Button("OK")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
}

auto ImportPanel::DrawDiagnosticsUi() -> void
{
  if (!ImGui::CollapsingHeader("Diagnostics")) {
    return;
  }

  std::lock_guard<std::mutex> lock(import_state_.progress_mutex);
  if (import_state_.diagnostics.empty()) {
    ImGui::TextDisabled("No diagnostics yet.");
    return;
  }

  const bool child_visible
    = ImGui::BeginChild("ImportDiagnostics", ImVec2(0.0F, 140.0F), true);
  if (child_visible) {
    for (const auto& diag : import_state_.diagnostics) {
      const char* severity = "Info";
      if (diag.severity == content::import::ImportSeverity::kWarning) {
        severity = "Warning";
      } else if (diag.severity == content::import::ImportSeverity::kError) {
        severity = "Error";
      }
      ImGui::TextWrapped("[%s] %s", severity, diag.message.c_str());
    }
  }
  ImGui::EndChild();
}

auto ImportPanel::DrawJobSummaryUi() -> void
{
  if (!ImGui::CollapsingHeader("Last Import Summary")) {
    return;
  }

  if (!last_report_.has_value()) {
    ImGui::TextDisabled("No completed import yet.");
    return;
  }

  ImGui::Text("Cooked root: %s", last_report_->cooked_root.string().c_str());
  ImGui::Text("Scenes: %u", last_report_->scenes_written);
  ImGui::Text("Geometry: %u", last_report_->geometry_written);
  ImGui::Text("Materials: %u", last_report_->materials_written);
  ImGui::Text("Success: %s", last_report_->success ? "Yes" : "No");
}

} // namespace oxygen::examples::render_scene::ui
