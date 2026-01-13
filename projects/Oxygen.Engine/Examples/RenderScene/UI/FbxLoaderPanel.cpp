//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "FbxLoaderPanel.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <system_error>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Data/AssetType.h>

#include "FilePicker.h"

namespace oxygen::examples::render_scene::ui {

namespace {

  [[nodiscard]] auto BeginEnumCombo(const char* label, const char* preview)
    -> bool
  {
    return ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest);
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

    const char* preview = to_string(value);
    bool changed = false;
    if (BeginEnumCombo(label, preview)) {
      for (const auto candidate : kFormats) {
        const bool is_selected = (candidate == value);
        if (ImGui::Selectable(to_string(candidate), is_selected)) {
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

  template <typename EnumT, std::size_t N>
  auto DrawEnumCombo(
    const char* label, EnumT& value, const std::array<EnumT, N>& items) -> bool
  {
    const char* preview = to_string(value);
    bool changed = false;
    if (BeginEnumCombo(label, preview)) {
      for (const auto candidate : items) {
        const bool is_selected = (candidate == value);
        if (ImGui::Selectable(to_string(candidate), is_selected)) {
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

} // namespace

void FbxLoaderPanel::Initialize(const FbxLoaderConfig& config)
{
  config_ = config;
  files_cached_ = false;
  cached_fbx_files_.clear();
}

auto FbxLoaderPanel::EnumerateFbxFiles() const
  -> std::vector<std::filesystem::path>
{
  std::vector<std::filesystem::path> files;

  std::error_code ec;
  if (!std::filesystem::exists(config_.fbx_directory, ec)
    || !std::filesystem::is_directory(config_.fbx_directory, ec)) {
    return files;
  }

  for (const auto& entry :
    std::filesystem::directory_iterator(config_.fbx_directory, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto path = entry.path();
    if (path.extension() == ".fbx") {
      files.push_back(path);
    }
  }

  std::sort(files.begin(), files.end(),
    [](const std::filesystem::path& a, const std::filesystem::path& b) {
      return a.filename().string() < b.filename().string();
    });

  return files;
}

void FbxLoaderPanel::StartImport(const std::filesystem::path& fbx_path)
{
  if (import_state_.is_importing.load(std::memory_order_relaxed)) {
    return;
  }

  import_state_.importing_path = fbx_path.string();
  import_state_.is_importing.store(true, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.completion_ready.store(false, std::memory_order_relaxed);

  const auto cooked_root
    = std::filesystem::absolute(config_.cooked_output_directory);

  const auto texture_tuning = texture_tuning_;

  import_state_.import_thread
    = std::jthread([this, fbx_path, cooked_root, texture_tuning](
                     const std::stop_token stop_token) -> void {
        FbxImportState::Completion completion {};

        std::error_code ec;
        (void)std::filesystem::create_directories(cooked_root, ec);

        if (stop_token.stop_requested()) {
          completion.cancelled = true;
        } else {
          auto importer = std::make_unique<content::import::AssetImporter>();

          try {
            content::import::ImportRequest request {};
            request.source_path = fbx_path;
            request.cooked_root = cooked_root;
            request.options.naming_strategy
              = std::make_shared<content::import::NormalizeNamingStrategy>();
            request.options.texture_tuning = texture_tuning;
            request.options.stop_token = stop_token;

            (void)importer->ImportToLooseCooked(request);

            if (stop_token.stop_requested()) {
              completion.cancelled = true;
            } else {
              auto inspection
                = std::make_unique<content::LooseCookedInspection>();
              completion.index_path
                = cooked_root / request.loose_cooked_layout.index_file_name;
              inspection->LoadFromFile(completion.index_path);

              const auto expected_scene_name = fbx_path.stem().string();
              const auto expected_virtual_path
                = request.loose_cooked_layout.SceneVirtualPath(
                  expected_scene_name);

              std::optional<data::AssetKey> matching_scene_key;
              std::optional<data::AssetKey> first_scene_key;
              std::string first_scene_path;

              for (const auto& asset : inspection->Assets()) {
                if (stop_token.stop_requested()) {
                  completion.cancelled = true;
                  break;
                }

                if (asset.asset_type
                  != static_cast<uint8_t>(data::AssetType::kScene)) {
                  continue;
                }

                if (asset.virtual_path == expected_virtual_path) {
                  matching_scene_key = asset.key;
                }

                if (!first_scene_key || asset.virtual_path < first_scene_path) {
                  first_scene_key = asset.key;
                  first_scene_path = asset.virtual_path;
                }
              }

              if (!completion.cancelled) {
                completion.scene_key
                  = matching_scene_key ? matching_scene_key : first_scene_key;
              }
            }

          } catch (const std::exception& e) {
            if (stop_token.stop_requested()) {
              completion.cancelled = true;
            } else {
              completion.error = e.what();
            }
          }
        }

        {
          std::lock_guard<std::mutex> lock(import_state_.completion_mutex);
          import_state_.completion = std::move(completion);
        }
        import_state_.is_importing.store(false, std::memory_order_relaxed);
        import_state_.completion_ready.store(true, std::memory_order_relaxed);
      });
}

void FbxLoaderPanel::CancelImport()
{
  if (!import_state_.is_importing.load(std::memory_order_relaxed)) {
    return;
  }

  import_state_.cancel_requested.store(true, std::memory_order_relaxed);
  if (import_state_.import_thread.joinable()) {
    import_state_.import_thread.request_stop();
  }
}

void FbxLoaderPanel::Update()
{
  if (!import_state_.completion_ready.load(std::memory_order_relaxed)) {
    return;
  }

  FbxImportState::Completion completion;
  {
    std::lock_guard<std::mutex> lock(import_state_.completion_mutex);
    completion = import_state_.completion;
    import_state_.completion = {};
  }

  import_state_.completion_ready.store(false, std::memory_order_relaxed);
  import_state_.cancel_requested.store(false, std::memory_order_relaxed);
  import_state_.importing_path.clear();
  import_state_.import_thread = {};

  if (completion.cancelled) {
    LOG_F(INFO, "FBX import cancelled");
    return;
  }

  if (!completion.error.empty()) {
    LOG_F(ERROR, "FBX import failed: {}", completion.error);
    return;
  }

  if (!completion.scene_key) {
    LOG_F(ERROR, "FBX import failed or produced no scene");
    return;
  }

  if (config_.on_index_loaded && !completion.index_path.empty()) {
    config_.on_index_loaded(completion.index_path);
  }

  if (config_.on_scene_ready) {
    config_.on_scene_ready(*completion.scene_key);

    if (auto_dump_texture_memory_ && config_.on_dump_texture_memory
      && dump_top_n_ > 0) {
      pending_auto_dump_frames_ = auto_dump_delay_frames_;
    }
  }
}

auto FbxLoaderPanel::DrawTextureTuningUi() -> void
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

  (void)DrawEnumCombo("Mip policy", texture_tuning_.mip_policy, kMipPolicies);
  if (texture_tuning_.mip_policy == content::import::MipPolicy::kMaxCount) {
    int max_mips = static_cast<int>(texture_tuning_.max_mip_levels);
    max_mips = (std::max)(1, (std::min)(max_mips, 16));
    if (ImGui::SliderInt("Max mip levels", &max_mips, 1, 16)) {
      texture_tuning_.max_mip_levels = static_cast<uint8_t>(max_mips);
    }
  }

  (void)DrawEnumCombo("Mip filter", texture_tuning_.mip_filter, kMipFilters);

  ImGui::Separator();
  (void)DrawFormatCombo(
    "Color output format", texture_tuning_.color_output_format);
  (void)DrawFormatCombo(
    "Data output format", texture_tuning_.data_output_format);

  (void)DrawEnumCombo(
    "BC7 quality", texture_tuning_.bc7_quality, kBc7Qualities);
  (void)DrawPackingPolicyCombo(texture_tuning_.packing_policy_id);

  ImGui::Separator();
  ImGui::Checkbox(
    "Auto-dump runtime texture VRAM after import", &auto_dump_texture_memory_);
  ImGui::SliderInt("Dump Top N", &dump_top_n_, 1, 200);
  ImGui::SliderInt("Dump delay (frames)", &auto_dump_delay_frames_, 0, 600);
}

void FbxLoaderPanel::Draw()
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
    ImGui::Text("Importing FBX: %s", import_state_.importing_path.c_str());

    if (import_state_.cancel_requested.load(std::memory_order_relaxed)) {
      ImGui::TextDisabled("Cancelling...");
    } else {
      if (ImGui::Button("Cancel import")) {
        CancelImport();
      }
    }

    // Animated progress bar - stretch to fill available width
    const float time = static_cast<float>(ImGui::GetTime());
    ImGui::ProgressBar(
      -1.0f * time * 0.2f, ImVec2(-1.0f, 0.0f), "Importing...");
    return;
  }

  // Cache file list on first draw or when requested
  if (!files_cached_) {
    cached_fbx_files_ = EnumerateFbxFiles();
    files_cached_ = true;
  }

  // File picker button
#if defined(OXYGEN_WINDOWS)
  if (ImGui::Button("Browse for FBX...")) {
    auto picker_config = MakeFbxFilePickerConfig();
    picker_config.initial_directory = config_.fbx_directory;

    if (const auto selected_path = ShowFilePicker(picker_config)) {
      StartImport(*selected_path);
      return;
    }
  }
  ImGui::SameLine();
#endif
  if (ImGui::Button("Refresh List")) {
    files_cached_ = false;
  }

  ImGui::Separator();
  DrawTextureTuningUi();

  if (config_.on_dump_texture_memory) {
    if (ImGui::Button("Dump runtime texture VRAM now")) {
      config_.on_dump_texture_memory(static_cast<std::size_t>(dump_top_n_));
    }
  }

  // FBX files list - stretch to fill available space
  const float available_height = ImGui::GetContentRegionAvail().y;
  if (ImGui::BeginListBox("##FbxFiles", ImVec2(-1.0f, available_height))) {
    for (const auto& fbx_path : cached_fbx_files_) {
      const auto filename = fbx_path.filename().string();

      if (ImGui::Selectable(filename.c_str(), false)) {
        StartImport(fbx_path);
      }

      // Tooltip with full path
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", fbx_path.string().c_str());
      }
    }
    ImGui::EndListBox();
  }

  if (cached_fbx_files_.empty()) {
    ImGui::TextDisabled("No FBX files found in directory");
    ImGui::TextDisabled(
      "Directory: %s", config_.fbx_directory.string().c_str());
  }
}

} // namespace oxygen::examples::render_scene::ui
