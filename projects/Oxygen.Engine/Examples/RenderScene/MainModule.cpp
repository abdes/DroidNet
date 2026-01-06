//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Import/AssetImporter.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/Flags.h>

using oxygen::scene::SceneNodeFlags;

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace {
#if defined(OXYGEN_WINDOWS)

class ScopedCoInitialize {
public:
  ScopedCoInitialize()
  {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(hr);
    // If COM is already initialized in a different mode, we proceed without
    // owning CoUninitialize() for this scope.
    if (hr == RPC_E_CHANGED_MODE) {
      initialized_ = false;
    }
  }

  ~ScopedCoInitialize()
  {
    if (initialized_) {
      CoUninitialize();
    }
  }

  ScopedCoInitialize(const ScopedCoInitialize&) = delete;
  ScopedCoInitialize& operator=(const ScopedCoInitialize&) = delete;
  ScopedCoInitialize(ScopedCoInitialize&&) = delete;
  ScopedCoInitialize& operator=(ScopedCoInitialize&&) = delete;

private:
  bool initialized_ { false };
};

auto TryBrowseForPakFile(std::string& out_utf8_path) -> bool
{
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    return false;
  }

  constexpr COMDLG_FILTERSPEC kFilters[] = {
    { L"Oxygen PAK files (*.pak)", L"*.pak" },
    { L"All files (*.*)", L"*.*" },
  };
  (void)dlg->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
  (void)dlg->SetDefaultExtension(L"pak");

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

auto TryBrowseForLooseCookedIndexFile(std::string& out_utf8_path) -> bool
{
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    return false;
  }

  constexpr COMDLG_FILTERSPEC kFilters[] = {
    { L"Loose cooked index (container.index.bin)", L"container.index.bin" },
    { L"Binary files (*.bin)", L"*.bin" },
    { L"All files (*.*)", L"*.*" },
  };
  (void)dlg->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
  (void)dlg->SetDefaultExtension(L"bin");

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

#endif

auto MakeLookRotationFromPosition(const glm::vec3& position,
  const glm::vec3& target, const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F })
  -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto forward = glm::normalize(forward_raw);
  // Avoid singularities when forward is colinear with up.
  glm::vec3 up_dir = up_direction;
  const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
  if (dot_abs > 0.999F) {
    // Pick an alternate up that is guaranteed to be non-colinear.
    up_dir = (std::abs(forward.z) > 0.9F) ? glm::vec3(0.0F, 1.0F, 0.0F)
                                          : glm::vec3(0.0F, 0.0F, 1.0F);
  }

  const auto right_raw = glm::cross(forward, up_dir);
  const float right_len2 = glm::dot(right_raw, right_raw);
  if (right_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto right = right_raw / std::sqrt(right_len2);
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(look_matrix);
}

auto FindRenderSceneContentRoot() -> std::filesystem::path
{
  auto dir = std::filesystem::current_path();
  for (int i = 0; i < 6; ++i) {
    const auto direct_fbx = dir / "fbx";
    if (std::filesystem::exists(direct_fbx)
      && std::filesystem::is_directory(direct_fbx)) {
      return dir;
    }

    const auto nested_root = dir / "Examples" / "RenderScene";
    const auto nested_fbx = nested_root / "fbx";
    if (std::filesystem::exists(nested_fbx)
      && std::filesystem::is_directory(nested_fbx)) {
      return nested_root;
    }

    if (!dir.has_parent_path()) {
      break;
    }
    const auto parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }

  return std::filesystem::current_path();
}

auto EnumerateFbxFiles(const std::filesystem::path& fbx_dir)
  -> std::vector<std::filesystem::path>
{
  std::vector<std::filesystem::path> files;

  std::error_code ec;
  if (!std::filesystem::exists(fbx_dir, ec)
    || !std::filesystem::is_directory(fbx_dir, ec)) {
    return files;
  }

  for (const auto& entry : std::filesystem::directory_iterator(fbx_dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto p = entry.path();
    if (p.extension() == ".fbx") {
      files.push_back(p);
    }
  }

  std::sort(files.begin(), files.end(),
    [](const std::filesystem::path& a, const std::filesystem::path& b) {
      return a.filename().string() < b.filename().string();
    });

  return files;
}

} // namespace

namespace oxygen::examples::render_scene {

auto MainModule::UpdateActiveCameraInputContext() -> void
{
  if (!app_.input_system) {
    return;
  }

  if (camera_mode_ == CameraMode::kOrbit) {
    if (orbit_controls_ctx_) {
      app_.input_system->ActivateMappingContext(orbit_controls_ctx_);
    }
    if (fly_controls_ctx_) {
      app_.input_system->DeactivateMappingContext(fly_controls_ctx_);
    }
  } else {
    if (orbit_controls_ctx_) {
      app_.input_system->DeactivateMappingContext(orbit_controls_ctx_);
    }
    if (fly_controls_ctx_) {
      app_.input_system->ActivateMappingContext(fly_controls_ctx_);
    }
  }
}

class SceneLoader : public std::enable_shared_from_this<SceneLoader> {
public:
  SceneLoader(
    oxygen::content::AssetLoader& loader, const int width, const int height)
    : loader_(loader)
    , width_(width)
    , height_(height)
  {
  }

  ~SceneLoader() { LOG_F(INFO, "SceneLoader: Destroying loader."); }

  void Start(const data::AssetKey& key)
  {
    LOG_F(INFO, "SceneLoader: Starting load for scene key: {}",
      oxygen::data::to_string(key));

    swap_.scene_key = key;
    // Start loading the scene asset
    loader_.StartLoadAsset<data::SceneAsset>(key,
      [weak_self = weak_from_this()](std::shared_ptr<data::SceneAsset> asset) {
        if (auto self = weak_self.lock()) {
          self->OnSceneLoaded(std::move(asset));
        }
      });
  }

  [[nodiscard]] auto IsReady() const -> bool { return ready_ && !consumed_; }
  [[nodiscard]] auto IsFailed() const -> bool { return failed_; }
  [[nodiscard]] auto IsConsumed() const -> bool { return consumed_; }

  auto GetResult() -> MainModule::PendingSceneSwap { return std::move(swap_); }

  void MarkConsumed()
  {
    consumed_ = true;
    linger_frames_ = 2;
  }

  auto Tick() -> bool
  {
    if (consumed_) {
      if (linger_frames_ > 0) {
        linger_frames_--;
        return false;
      }
      return true;
    }
    return false;
  }

private:
  void OnSceneLoaded(std::shared_ptr<data::SceneAsset> asset)
  {
    if (!asset) {
      LOG_F(ERROR, "SceneLoader: Failed to load scene asset");
      failed_ = true;
      return;
    }

    LOG_F(INFO, "SceneLoader: Scene asset loaded. Instantiating nodes...");

    swap_.scene = std::make_shared<scene::Scene>("RenderScene");

    // Instantiate nodes (synchronous part)
    using oxygen::data::pak::DirectionalLightRecord;
    using oxygen::data::pak::NodeRecord;
    using oxygen::data::pak::OrthographicCameraRecord;
    using oxygen::data::pak::PerspectiveCameraRecord;
    using oxygen::data::pak::PointLightRecord;
    using oxygen::data::pak::RenderableRecord;
    using oxygen::data::pak::SpotLightRecord;

    const auto nodes = asset->GetNodes();
    runtime_nodes_.reserve(nodes.size());

    LOG_F(INFO,
      "SceneLoader: Scene summary: nodes={} renderables={} "
      "perspective_cameras={} orthographic_cameras={} "
      "directional_lights={} point_lights={} spot_lights={}",
      nodes.size(), asset->GetComponents<RenderableRecord>().size(),
      asset->GetComponents<PerspectiveCameraRecord>().size(),
      asset->GetComponents<OrthographicCameraRecord>().size(),
      asset->GetComponents<DirectionalLightRecord>().size(),
      asset->GetComponents<PointLightRecord>().size(),
      asset->GetComponents<SpotLightRecord>().size());

    for (size_t i = 0; i < nodes.size(); ++i) {
      const NodeRecord& node = nodes[i];

      std::string_view name_view = asset->GetNodeName(node);
      std::string name;
      if (name_view.empty()) {
        name = "Node" + std::to_string(i);
      } else {
        name.assign(name_view.begin(), name_view.end());
      }

      auto n = swap_.scene->CreateNode(name);
      auto tf = n.GetTransform();
      tf.SetLocalPosition(glm::vec3(
        node.translation[0], node.translation[1], node.translation[2]));
      tf.SetLocalRotation(glm::quat(node.rotation[3], node.rotation[0],
        node.rotation[1], node.rotation[2]));
      tf.SetLocalScale(glm::vec3(node.scale[0], node.scale[1], node.scale[2]));

      runtime_nodes_.push_back(std::move(n));
    }

    // Apply hierarchy using parent indices.
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto parent_index = static_cast<size_t>(nodes[i].parent_index);
      if (parent_index == i) {
        continue;
      }
      if (parent_index >= runtime_nodes_.size()) {
        LOG_F(WARNING, "Invalid parent_index {} for node {}", parent_index, i);
        continue;
      }

      const bool ok = swap_.scene->ReparentNode(runtime_nodes_[i],
        runtime_nodes_[parent_index], /*preserve_world_transform=*/false);
      if (!ok) {
        LOG_F(WARNING, "Failed to reparent node {} under {}", i, parent_index);
      }
    }

    // Identify renderables and assign geometries (synchronous)
    const auto renderables = asset->GetComponents<RenderableRecord>();
    int valid_renderables = 0;
    for (const RenderableRecord& r : renderables) {
      if (r.visible == 0) {
        continue;
      }
      const auto node_index = static_cast<size_t>(r.node_index);
      if (node_index >= runtime_nodes_.size()) {
        continue;
      }

      // AssetLoader guarantees dependencies are loaded (or placeholders are
      // ready). We retrieve the asset directly to support placeholders and
      // avoid redundant async waits.
      auto geo = loader_.GetGeometryAsset(r.geometry_key);
      if (geo) {
        runtime_nodes_[node_index].GetRenderable().SetGeometry(std::move(geo));
        valid_renderables++;
      } else {
        LOG_F(WARNING, "SceneLoader: Missing geometry dependency for node {}",
          node_index);
      }
    }

    if (valid_renderables > 0) {
      LOG_F(INFO, "SceneLoader: Assigned {} geometries from cache.",
        valid_renderables);
    }

    // Instantiate light components (synchronous).
    const auto ApplyCommonLight
      = [](scene::CommonLightProperties& dst,
          const oxygen::data::pak::LightCommonRecord& src) {
          dst.affects_world = (src.affects_world != 0U);
          dst.color_rgb
            = { src.color_rgb[0], src.color_rgb[1], src.color_rgb[2] };
          dst.intensity = src.intensity;
          dst.mobility = static_cast<scene::LightMobility>(src.mobility);
          dst.casts_shadows = (src.casts_shadows != 0U);
          dst.shadow.bias = src.shadow.bias;
          dst.shadow.normal_bias = src.shadow.normal_bias;
          dst.shadow.contact_shadows = (src.shadow.contact_shadows != 0U);
          dst.shadow.resolution_hint = static_cast<scene::ShadowResolutionHint>(
            src.shadow.resolution_hint);
          dst.exposure_compensation_ev = src.exposure_compensation_ev;
        };

    int attached_directional = 0;
    for (const DirectionalLightRecord& rec :
      asset->GetComponents<DirectionalLightRecord>()) {
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index >= runtime_nodes_.size()) {
        continue;
      }

      auto light = std::make_unique<scene::DirectionalLight>();
      ApplyCommonLight(light->Common(), rec.common);
      light->SetAngularSizeRadians(rec.angular_size_radians);
      light->SetEnvironmentContribution(rec.environment_contribution != 0U);

      auto& csm = light->CascadedShadows();
      csm.cascade_count = std::clamp<std::uint32_t>(
        rec.cascade_count, 1U, scene::kMaxShadowCascades);
      for (std::uint32_t i = 0U; i < scene::kMaxShadowCascades; ++i) {
        // NOLINTNEXTLINE(*-pro-bounds-constant-array-index)
        csm.cascade_distances[i] = rec.cascade_distances[i];
      }
      csm.distribution_exponent = rec.distribution_exponent;

      const bool attached
        = runtime_nodes_[node_index].ReplaceLight(std::move(light));
      if (attached) {
        attached_directional++;
      } else {
        LOG_F(WARNING,
          "SceneLoader: Failed to attach DirectionalLight to node_index={}",
          node_index);
      }
    }

    // Ensure a sunlight exists even when the scene asset provides no valid
    // directional light component. Avoid LookAt() here because world transforms
    // are not guaranteed to be available during the load/instantiation phase.
    if (attached_directional == 0) {
      auto sun_node = swap_.scene->CreateNode("Sun");
      auto sun_tf = sun_node.GetTransform();
      sun_tf.SetLocalPosition(glm::vec3(0.0F, 0.0F, 0.0F));

      // Set a natural sun direction (angled, not straight down).
      // Convention: engine forward is -Y and Z-up.
      // We compute a rotation that maps local Forward (-Y) to the desired
      // world-space ray direction (from light toward the scene).
      const glm::vec3 from_dir(0.0F, -1.0F, 0.0F);
      const glm::vec3 to_dir = glm::normalize(glm::vec3(-1.0F, -0.6F, -1.4F));

      const float cos_theta
        = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);
      glm::quat sun_rot(1.0F, 0.0F, 0.0F, 0.0F);
      if (cos_theta < 0.9999F) {
        if (cos_theta > -0.9999F) {
          const glm::vec3 axis = glm::normalize(glm::cross(from_dir, to_dir));
          const float angle = std::acos(cos_theta);
          sun_rot = glm::angleAxis(angle, axis);
        } else {
          // Opposite vectors: pick a stable orthogonal axis.
          const glm::vec3 axis = glm::vec3(0.0F, 0.0F, 1.0F);
          sun_rot = glm::angleAxis(glm::pi<float>(), axis);
        }
      }

      sun_tf.SetLocalRotation(sun_rot);

      auto sun_light = std::make_unique<scene::DirectionalLight>();
      sun_light->Common().affects_world = true;
      sun_light->Common().color_rgb = { 1.0F, 0.98F, 0.92F };
      sun_light->Common().intensity = 2.0F;
      sun_light->Common().mobility = scene::LightMobility::kRealtime;
      sun_light->Common().casts_shadows = true;
      sun_light->SetAngularSizeRadians(0.01F);
      sun_light->SetEnvironmentContribution(true);

      const bool attached = sun_node.ReplaceLight(std::move(sun_light));
      if (!attached) {
        LOG_F(WARNING, "SceneLoader: Failed to attach fallback Sun light");
      } else {
        attached_directional++;
      }
    }

    int attached_point = 0;
    for (const PointLightRecord& rec :
      asset->GetComponents<PointLightRecord>()) {
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index >= runtime_nodes_.size()) {
        continue;
      }

      auto light = std::make_unique<scene::PointLight>();
      ApplyCommonLight(light->Common(), rec.common);
      light->SetRange(std::abs(rec.range));
      light->SetAttenuationModel(
        static_cast<scene::AttenuationModel>(rec.attenuation_model));
      light->SetDecayExponent(rec.decay_exponent);
      light->SetSourceRadius(std::abs(rec.source_radius));

      const bool attached
        = runtime_nodes_[node_index].ReplaceLight(std::move(light));
      if (attached) {
        attached_point++;
      } else {
        LOG_F(WARNING,
          "SceneLoader: Failed to attach PointLight to node_index={}",
          node_index);
      }
    }

    int attached_spot = 0;
    for (const SpotLightRecord& rec : asset->GetComponents<SpotLightRecord>()) {
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index >= runtime_nodes_.size()) {
        continue;
      }

      auto light = std::make_unique<scene::SpotLight>();
      ApplyCommonLight(light->Common(), rec.common);
      light->SetRange(std::abs(rec.range));
      light->SetAttenuationModel(
        static_cast<scene::AttenuationModel>(rec.attenuation_model));
      light->SetDecayExponent(rec.decay_exponent);
      light->SetConeAnglesRadians(
        rec.inner_cone_angle_radians, rec.outer_cone_angle_radians);
      light->SetSourceRadius(std::abs(rec.source_radius));

      const bool attached
        = runtime_nodes_[node_index].ReplaceLight(std::move(light));
      if (attached) {
        attached_spot++;
      } else {
        LOG_F(WARNING,
          "SceneLoader: Failed to attach SpotLight to node_index={}",
          node_index);
      }
    }

    if (attached_directional + attached_point + attached_spot > 0) {
      LOG_F(INFO,
        "SceneLoader: Attached lights: directional={} point={} spot={} "
        "(total={})",
        attached_directional, attached_point, attached_spot,
        attached_directional + attached_point + attached_spot);
    }

    // Pick or create an active camera.
    const auto perspective_cams
      = asset->GetComponents<PerspectiveCameraRecord>();
    if (!perspective_cams.empty()) {
      LOG_F(INFO, "SceneLoader: Found {} perspective camera(s)",
        perspective_cams.size());
      const auto& rec = perspective_cams.front();
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index < runtime_nodes_.size()) {
        swap_.active_camera = runtime_nodes_[node_index];
        LOG_F(INFO,
          "SceneLoader: Using perspective camera node_index={} name='{}'",
          rec.node_index, swap_.active_camera.GetName().c_str());
        if (!swap_.active_camera.HasCamera()) {
          auto cam = std::make_unique<scene::PerspectiveCamera>();
          const bool attached
            = swap_.active_camera.AttachCamera(std::move(cam));
          CHECK_F(attached,
            "Failed to attach PerspectiveCamera to scene camera node");
        }
        if (auto cam_ref
          = swap_.active_camera.GetCameraAs<scene::PerspectiveCamera>();
          cam_ref) {
          auto& cam = cam_ref->get();
          float near_plane = std::abs(rec.near_plane);
          float far_plane = std::abs(rec.far_plane);
          if (far_plane < near_plane) {
            std::swap(far_plane, near_plane);
          }
          cam.SetFieldOfView(rec.fov_y);

          cam.SetNearPlane(near_plane);
          cam.SetFarPlane(far_plane);

          const float fov_y_deg
            = rec.fov_y * (180.0F / std::numbers::pi_v<float>);
          LOG_F(INFO,
            "SceneLoader: Applied perspective camera params fov_y_deg={} "
            "near={} far={} aspect_hint={}",
            fov_y_deg, near_plane, far_plane, rec.aspect_ratio);

          auto tf = swap_.active_camera.GetTransform();
          glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
          glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
          if (auto lp = tf.GetLocalPosition()) {
            cam_pos = *lp;
          }
          if (auto lr = tf.GetLocalRotation()) {
            cam_rot = *lr;
          }
          const glm::vec3 forward = cam_rot * glm::vec3(0.0F, 0.0F, -1.0F);
          const glm::vec3 up = cam_rot * glm::vec3(0.0F, 1.0F, 0.0F);
          LOG_F(INFO,
            "SceneLoader: Camera local pose pos=({:.3f}, {:.3f}, {:.3f}) "
            "forward=({:.3f}, {:.3f}, {:.3f}) up=({:.3f}, {:.3f}, {:.3f})",
            cam_pos.x, cam_pos.y, cam_pos.z, forward.x, forward.y, forward.z,
            up.x, up.y, up.z);
        }
      }
    }

    // If no perspective, try ortho
    if (!swap_.active_camera.IsAlive()) {
      const auto ortho_cams = asset->GetComponents<OrthographicCameraRecord>();
      if (!ortho_cams.empty()) {
        LOG_F(INFO, "SceneLoader: Found {} orthographic camera(s)",
          ortho_cams.size());
        const auto& rec = ortho_cams.front();
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index < runtime_nodes_.size()) {
          swap_.active_camera = runtime_nodes_[node_index];
          LOG_F(INFO,
            "SceneLoader: Using orthographic camera node_index={} name='{}'",
            rec.node_index, swap_.active_camera.GetName().c_str());
          if (!swap_.active_camera.HasCamera()) {
            auto cam = std::make_unique<scene::OrthographicCamera>();
            const bool attached
              = swap_.active_camera.AttachCamera(std::move(cam));
            CHECK_F(attached,
              "Failed to attach OrthographicCamera to scene camera node");
          }
          if (auto cam_ref
            = swap_.active_camera.GetCameraAs<scene::OrthographicCamera>();
            cam_ref) {
            float near_plane = std::abs(rec.near_plane);
            float far_plane = std::abs(rec.far_plane);
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            cam_ref->get().SetExtents(
              rec.left, rec.right, rec.bottom, rec.top, near_plane, far_plane);
            LOG_F(INFO,
              "SceneLoader: Applied orthographic camera extents l={} r={} b={} "
              "t={} near={} far={}",
              rec.left, rec.right, rec.bottom, rec.top, near_plane, far_plane);
          }
        }
      }
    }

    // Finalize setup
    const float aspect = height_ > 0
      ? (static_cast<float>(width_) / static_cast<float>(height_))
      : 1.0F;

    const ViewPort viewport { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width_),
      .height = static_cast<float>(height_),
      .min_depth = 0.0F,
      .max_depth = 1.0F };

    // Ensure we have a camera if none was found in the scene
    if (!swap_.active_camera.IsAlive()) {
      swap_.active_camera = swap_.scene->CreateNode("MainCamera");
      // Stable, elevated pose: look at origin with Z-up.
      const glm::vec3 cam_pos(10.0F, 10.0F, 10.0F);
      const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
      auto tf = swap_.active_camera.GetTransform();
      tf.SetLocalPosition(cam_pos);
      tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));
      LOG_F(INFO,
        "SceneLoader: No camera in scene; created fallback camera '{}'",
        swap_.active_camera.GetName().c_str());
    }

    if (!swap_.active_camera.HasCamera()) {
      auto camera = std::make_unique<scene::PerspectiveCamera>();
      swap_.active_camera.AttachCamera(std::move(camera));
    }

    // Apply viewport to the active camera
    if (auto cam_ref
      = swap_.active_camera.GetCameraAs<scene::PerspectiveCamera>();
      cam_ref) {
      auto& cam = cam_ref->get();
      cam.SetAspectRatio(aspect);
      cam.SetViewport(viewport);
    } else if (auto ortho_ref
      = swap_.active_camera.GetCameraAs<scene::OrthographicCamera>();
      ortho_ref) {
      ortho_ref->get().SetViewport(viewport);
    }

    // Dump the runtime scene hierarchy (once per load).
    LOG_F(INFO, "SceneLoader: Runtime scene hierarchy:");
    const auto PrintNodeLine = [](scene::SceneNode& node, const int depth) {
      const std::string indent(static_cast<size_t>(depth * 2), ' ');
      const bool has_renderable = node.GetRenderable().HasGeometry();
      const bool has_camera = node.HasCamera();
      const bool has_light = node.HasLight();
      LOG_F(INFO, "{}- {}{}{}{}", indent, node.GetName().c_str(),
        has_renderable ? " [R]" : "", has_camera ? " [C]" : "",
        has_light ? " [L]" : "");
    };

    const auto PrintSubtree
      = [&](const auto& self, scene::SceneNode node, const int depth) -> void {
      if (!node.IsAlive()) {
        return;
      }

      PrintNodeLine(node, depth);

      auto child = node.GetFirstChild();
      while (child) {
        self(self, *child, depth + 1);
        child = child->GetNextSibling();
      }
    };

    for (auto& root : swap_.scene->GetRootNodes()) {
      PrintSubtree(PrintSubtree, root, 0);
    }

    ready_ = true;
    LOG_F(INFO,
      "SceneLoader: Scene loading and instantiation complete. Ready for swap.");
  }

  oxygen::content::AssetLoader& loader_;
  int width_;
  int height_;
  MainModule::PendingSceneSwap swap_;
  std::vector<scene::SceneNode> runtime_nodes_;
  bool ready_ { false };
  bool failed_ { false };
  bool consumed_ { false };
  int linger_frames_ { 0 };
};

MainModule::MainModule(const oxygen::examples::common::AsyncEngineApp& app)
  : Base(app)
{
  std::snprintf(pak_path_.data(), pak_path_.size(), "%s", "");
  std::snprintf(loose_index_path_.data(), loose_index_path_.size(), "%s", "");
  content_root_ = FindRenderSceneContentRoot();
}

MainModule::~MainModule() = default;

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  LOG_F(WARNING, "RenderScene: OnAttached; input_system={} engine={}",
    static_cast<const void*>(app_.input_system.get()),
    static_cast<const void*>(engine.get()));

  if (!InitInputBindings()) {
    LOG_F(WARNING, "RenderScene: InitInputBindings failed");
    return false;
  }

  // Ensure the correct mapping context is active for the initial mode.
  UpdateActiveCameraInputContext();

  content_root_ = FindRenderSceneContentRoot();
  asset_importer_ = std::make_unique<content::import::AssetImporter>();

  LOG_F(WARNING, "RenderScene: InitInputBindings ok");
  return true;
}

void MainModule::OnShutdown() noexcept
{
  ui_pak_.reset();
  scene_.reset();
  scene_loader_.reset();
  active_camera_ = {};
  registered_view_camera_ = scene::NodeHandle();
  UnregisterViewForRendering("module shutdown");
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (scene_loader_) {
    if (scene_loader_->IsReady()) {
      auto swap = scene_loader_->GetResult();
      LOG_F(WARNING, "RenderScene: Applying staged scene swap (scene_key={})",
        oxygen::data::to_string(swap.scene_key));
      UnregisterViewForRendering("scene swap");

      scene_ = std::move(swap.scene);
      active_camera_ = std::move(swap.active_camera);
      if (active_camera_.IsAlive()) {
        orbit_controller_ = std::make_unique<OrbitCameraController>();
        orbit_controller_->SyncFromTransform(active_camera_);
        fly_controller_ = std::make_unique<FlyCameraController>();
        fly_controller_->SetLookSensitivity(0.0015f);
        fly_controller_->SyncFromTransform(active_camera_);
      }
      registered_view_camera_ = scene::NodeHandle();
      scene_loader_->MarkConsumed();
    } else if (scene_loader_->IsFailed()) {
      LOG_F(ERROR, "RenderScene: Scene loading failed");
      scene_loader_.reset();
    } else if (scene_loader_->IsConsumed()) {
      if (scene_loader_->Tick()) {
        scene_loader_.reset();
      }
    }
  }

  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("RenderScene");
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this, &context](int w, int h) {
    last_viewport_w_ = w;
    last_viewport_h_ = h;
    EnsureActiveCameraViewport(w, h);

    if (pending_sync_active_camera_ && active_camera_.IsAlive()) {
      if (camera_mode_ == CameraMode::kOrbit && orbit_controller_) {
        orbit_controller_->SyncFromTransform(active_camera_);
      } else if (camera_mode_ == CameraMode::kFly && fly_controller_) {
        fly_controller_->SyncFromTransform(active_camera_);
      }

      pending_sync_active_camera_ = false;
    }
  });
  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (pending_mount_pak_) {
    pending_mount_pak_ = false;

    pak_scenes_.clear();
    ui_pak_.reset();

    const std::filesystem::path pak_path { std::string { pak_path_.data() } };
    if (!pak_path.empty()) {
      try {
        ui_pak_ = std::make_unique<content::PakFile>(pak_path);

        if (ui_pak_->HasBrowseIndex()) {
          for (const auto& be : ui_pak_->BrowseIndex()) {
            const auto entry = ui_pak_->FindEntry(be.asset_key);
            if (!entry) {
              continue;
            }
            if (entry->asset_type
              != static_cast<uint8_t>(data::AssetType::kScene)) {
              continue;
            }

            pak_scenes_.push_back(SceneListItem {
              .virtual_path = be.virtual_path,
              .key = be.asset_key,
            });
          }
          std::sort(pak_scenes_.begin(), pak_scenes_.end(),
            [](const SceneListItem& a, const SceneListItem& b) {
              return a.virtual_path < b.virtual_path;
            });
        }

        auto asset_loader
          = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
        if (asset_loader) {
          asset_loader->ClearMounts();
          asset_loader->AddPakFile(pak_path);
        }
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to open/mount PAK: {}", e.what());
        ui_pak_.reset();
        pak_scenes_.clear();
      }
    }
  }

  if (fbx_import_future_.valid()
    && fbx_import_future_.wait_for(std::chrono::seconds(0))
      == std::future_status::ready) {
    is_importing_fbx_ = false;
    const auto result = fbx_import_future_.get();
    if (result) {
      pending_scene_key_ = *result;
      pending_load_scene_ = true;

      const auto cooked_root
        = std::filesystem::absolute(content_root_ / ".cooked");

      if (!loose_inspection_) {
        loose_inspection_ = std::make_unique<content::LooseCookedInspection>();
      }
      const auto index_path = cooked_root / "container.index.bin";
      loose_inspection_->LoadFromFile(index_path);

      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        asset_loader->ClearMounts();
        asset_loader->AddLooseCookedRoot(cooked_root);
      }
    } else {
      LOG_F(ERROR, "RenderScene: FBX import failed or produced no scene.");
    }
  }

  if (pending_load_loose_index_) {
    pending_load_loose_index_ = false;

    loose_scenes_.clear();
    if (!loose_inspection_) {
      loose_inspection_ = std::make_unique<content::LooseCookedInspection>();
    }

    const std::filesystem::path index_path { std::string {
      loose_index_path_.data() } };
    if (!index_path.empty()) {
      try {
        loose_inspection_->LoadFromFile(index_path);
        for (const auto& a : loose_inspection_->Assets()) {
          if (a.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
            continue;
          }
          loose_scenes_.push_back(SceneListItem {
            .virtual_path = a.virtual_path,
            .key = a.key,
          });
        }
        std::sort(loose_scenes_.begin(), loose_scenes_.end(),
          [](const SceneListItem& a, const SceneListItem& b) {
            return a.virtual_path < b.virtual_path;
          });

        auto asset_loader
          = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
        if (asset_loader) {
          asset_loader->ClearMounts();
          asset_loader->AddLooseCookedRoot(index_path.parent_path());
        }
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to load loose cooked index: {}", e.what());
        loose_scenes_.clear();
      }
    }
  }

  if (pending_load_scene_) {
    pending_load_scene_ = false;

    if (pending_scene_key_) {
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        scene_loader_ = std::make_shared<SceneLoader>(
          *asset_loader, last_viewport_w_, last_viewport_h_);
        scene_loader_->Start(*pending_scene_key_);
        LOG_F(WARNING, "RenderScene: Started async scene load (scene_key={})",
          oxygen::data::to_string(*pending_scene_key_));
      } else {
        LOG_F(ERROR, "AssetLoader unavailable");
      }
    }
  }

  co_return;
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(WARNING, "RenderScene: OnGameplay is running");
  }

  // Input edges are finalized during kInput earlier in the frame (mirrors the
  // InputSystem example). Apply camera controls here so WASD/Shift/Space and
  // mouse deltas are visible in the same frame.
  ApplyOrbitAndZoom(context.GetGameDeltaTime());

  co_return;
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerPulse;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using oxygen::platform::InputSlots;

  if (!app_.input_system) {
    LOG_F(WARNING, "RenderScene: InputSystem not available; no input bindings");
    return false;
  }

  LOG_F(WARNING, "RenderScene: Creating camera input actions");

  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  rmb_action_ = std::make_shared<Action>("rmb", ActionValueType::kBool);
  orbit_action_
    = std::make_shared<Action>("camera orbit", ActionValueType::kAxis2D);
  move_fwd_action_
    = std::make_shared<Action>("move fwd", ActionValueType::kBool);
  move_bwd_action_
    = std::make_shared<Action>("move bwd", ActionValueType::kBool);
  move_left_action_
    = std::make_shared<Action>("move left", ActionValueType::kBool);
  move_right_action_
    = std::make_shared<Action>("move right", ActionValueType::kBool);
  move_up_action_ = std::make_shared<Action>("move up", ActionValueType::kBool);
  move_down_action_
    = std::make_shared<Action>("move down", ActionValueType::kBool);
  fly_plane_lock_action_
    = std::make_shared<Action>("fly plane lock", ActionValueType::kBool);
  fly_boost_action_
    = std::make_shared<Action>("fly boost", ActionValueType::kBool);

  app_.input_system->AddAction(zoom_in_action_);
  app_.input_system->AddAction(zoom_out_action_);
  app_.input_system->AddAction(rmb_action_);
  app_.input_system->AddAction(orbit_action_);
  app_.input_system->AddAction(move_fwd_action_);
  app_.input_system->AddAction(move_bwd_action_);
  app_.input_system->AddAction(move_left_action_);
  app_.input_system->AddAction(move_right_action_);
  app_.input_system->AddAction(move_up_action_);
  app_.input_system->AddAction(move_down_action_);
  app_.input_system->AddAction(fly_plane_lock_action_);
  app_.input_system->AddAction(fly_boost_action_);

  LOG_F(
    WARNING, "RenderScene: Added actions (zoom_in/zoom_out/rmb/orbit/move)");

  // Orbit-only mapping context: wheel zoom + orbit/look (MouseXY gated by RMB)
  orbit_controls_ctx_ = std::make_shared<InputMappingContext>("camera orbit");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // RMB helper mapping
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // Orbit mapping: MouseXY with an implicit chain requiring RMB.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      orbit_controls_ctx_->AddMapping(mapping);
    }
  }

  // Fly-only mapping context: keyboard movement + mouse-look (MouseXY gated by
  // RMB). We keep the same actions, but isolate the mappings.
  fly_controls_ctx_ = std::make_shared<InputMappingContext>("camera fly");
  {
    // RMB helper mapping (shared action)
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      fly_controls_ctx_->AddMapping(mapping);
    }

    // Mouse look mapping: MouseXY with RMB prerequisite.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      fly_controls_ctx_->AddMapping(mapping);
    }

    auto add_bool_mapping = [&](const std::shared_ptr<Action>& action,
                              const auto& slot) {
      const auto mapping = std::make_shared<InputActionMapping>(action, slot);
      const auto trigger = std::make_shared<ActionTriggerPulse>();
      trigger->MakeExplicit();
      trigger->SetActuationThreshold(0.1F);
      mapping->AddTrigger(trigger);
      fly_controls_ctx_->AddMapping(mapping);
    };

    add_bool_mapping(move_fwd_action_, InputSlots::W);
    add_bool_mapping(move_bwd_action_, InputSlots::S);
    add_bool_mapping(move_left_action_, InputSlots::A);
    add_bool_mapping(move_right_action_, InputSlots::D);
    add_bool_mapping(move_up_action_, InputSlots::E);
    add_bool_mapping(move_down_action_, InputSlots::Q);
    add_bool_mapping(fly_plane_lock_action_, InputSlots::Space);
    add_bool_mapping(fly_boost_action_, InputSlots::LeftShift);
  }

  // Register both contexts; only one will be active at a time.
  app_.input_system->AddMappingContext(orbit_controls_ctx_, 10);
  app_.input_system->AddMappingContext(fly_controls_ctx_, 10);
  UpdateActiveCameraInputContext();

  LOG_F(WARNING,
    "RenderScene: Registered camera input contexts (orbit+fly) priority={} ",
    10);

  return true;
}

auto MainModule::ApplyOrbitAndZoom(time::CanonicalDuration delta_time) -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  if (camera_mode_ == CameraMode::kOrbit) {
    if (!orbit_controller_) {
      return;
    }

    // Zoom via mouse wheel actions
    if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(1.0f);
    }
    if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(-1.0f);
    }

    // Orbit via MouseXY deltas for this frame
    if (orbit_action_
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      glm::vec2 orbit_delta(0.0f);
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        orbit_delta.x += v.x;
        orbit_delta.y += v.y;
      }

      if (std::abs(orbit_delta.x) > 0.0f || std::abs(orbit_delta.y) > 0.0f) {
        orbit_controller_->AddOrbitInput(orbit_delta);
      }
    }

    orbit_controller_->Update(active_camera_, delta_time);
  } else if (camera_mode_ == CameraMode::kFly) {
    if (!fly_controller_) {
      return;
    }

    if (fly_boost_action_) {
      fly_controller_->SetBoostActive(fly_boost_action_->IsOngoing());
    }
    if (fly_plane_lock_action_) {
      fly_controller_->SetPlaneLockActive(fly_plane_lock_action_->IsOngoing());
    }

    // Zoom via mouse wheel actions (adjust speed)
    if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
      const float speed = fly_controller_->GetMoveSpeed();
      fly_controller_->SetMoveSpeed(std::min(speed * 1.2f, 1000.0f));
    }
    if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
      const float speed = fly_controller_->GetMoveSpeed();
      fly_controller_->SetMoveSpeed(std::max(speed / 1.2f, 0.1f));
    }

    // Look via MouseXY deltas
    if (orbit_action_
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      glm::vec2 look_delta(0.0f);
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        look_delta.x += v.x;
        look_delta.y += v.y;
      }

      if (std::abs(look_delta.x) > 0.0f || std::abs(look_delta.y) > 0.0f) {
        fly_controller_->AddRotationInput(look_delta);
      }
    }

    // Move via WASD/QE
    glm::vec3 move_input(0.0f);
    if (move_fwd_action_ && move_fwd_action_->IsOngoing()) {
      move_input.z += 1.0f;
    }
    if (move_bwd_action_ && move_bwd_action_->IsOngoing()) {
      move_input.z -= 1.0f;
    }
    if (move_left_action_ && move_left_action_->IsOngoing()) {
      move_input.x -= 1.0f;
    }
    if (move_right_action_ && move_right_action_->IsOngoing()) {
      move_input.x += 1.0f;
    }
    if (move_up_action_ && move_up_action_->IsOngoing()) {
      move_input.y += 1.0f;
    }
    if (move_down_action_ && move_down_action_->IsOngoing()) {
      move_input.y -= 1.0f;
    }

    if (glm::length(move_input) > 0.0f) {
      fly_controller_->AddMovementInput(move_input);
    }

    fly_controller_->Update(active_camera_, delta_time);
  }
}

auto MainModule::EnsureViewCameraRegistered() -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  const auto camera_handle = active_camera_.GetHandle();
  if (registered_view_camera_ != camera_handle) {
    registered_view_camera_ = camera_handle;
    UnregisterViewForRendering("camera changed");
    LOG_F(WARNING, "RenderScene: Active camera changed; re-registering view");
  }

  RegisterViewForRendering(active_camera_);
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }
  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;

  if (!imgui_module_ref) {
    co_return;
  }
  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    co_return;
  }
  auto* imgui_context = imgui_module.GetImGuiContext();
  if (imgui_context == nullptr) {
    co_return;
  }
  ImGui::SetCurrentContext(imgui_context);

  DrawDebugOverlay(context);
  DrawCameraControls(context);
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();
  }

  EnsureViewCameraRegistered();
  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::EnsureFallbackCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;

  if (!scene_) {
    return;
  }

  if (!active_camera_.IsAlive()) {
    active_camera_ = scene_->CreateNode("MainCamera");

    // Start with a stable, non-singular pose: look along the Y axis with Z-up.
    // This makes it unambiguous whether imported assets are rotated.
    const glm::vec3 cam_pos(10.0F, 10.0F, 10.0F);
    const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));

    orbit_controller_ = std::make_unique<OrbitCameraController>();
    orbit_controller_->SyncFromTransform(active_camera_);
    fly_controller_ = std::make_unique<FlyCameraController>();
    fly_controller_->SyncFromTransform(active_camera_);
  }

  if (!active_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = active_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  EnsureActiveCameraViewport(width, height);
}

auto MainModule::EnsureActiveCameraViewport(const int width, const int height)
  -> void
{
  if (!active_camera_.IsAlive()) {
    EnsureFallbackCamera(width, height);
    return;
  }

  const float aspect = height > 0
    ? (static_cast<float>(width) / static_cast<float>(height))
    : 1.0F;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F };

  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    auto& cam = cam_ref->get();
    cam.SetAspectRatio(aspect);
    cam.SetViewport(viewport);
    return;
  }

  if (auto ortho_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
    ortho_ref) {
    ortho_ref->get().SetViewport(viewport);
    return;
  }

  EnsureFallbackCamera(width, height);
}

auto MainModule::DrawDebugOverlay(engine::FrameContext& /*context*/) -> void
{
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(520, 250), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "RenderScene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  constexpr float kScenesListWidth = 480.0F;
  constexpr float kScenesListHeight = 220.0F;

  if (ImGui::BeginTabBar("ContentSource")) {
    if (ImGui::BeginTabItem("FBX")) {
      if (is_importing_fbx_) {
        ImGui::Text("Importing FBX: %s", importing_fbx_path_.c_str());
        // Indeterminate progress bar
        const float time = static_cast<float>(ImGui::GetTime());
        ImGui::ProgressBar(
          -1.0f * time * 0.2f, ImVec2(kScenesListWidth, 0.0f), "Importing...");
      } else {
        const auto fbx_dir = content_root_ / "fbx";
        const auto files = EnumerateFbxFiles(fbx_dir);

        if (ImGui::BeginListBox(
              "FBX##Fbx", ImVec2(kScenesListWidth, kScenesListHeight))) {
          for (const auto& p : files) {
            const auto name = p.filename().string();
            if (ImGui::Selectable(name.c_str(), false)) {
              importing_fbx_path_ = p.string();
              is_importing_fbx_ = true;

              const auto cooked_root
                = std::filesystem::absolute(content_root_ / ".cooked");

              fbx_import_future_ = std::async(std::launch::async,
                [p, cooked_root]() -> std::optional<data::AssetKey> {
                  std::error_code ec;
                  (void)std::filesystem::create_directories(cooked_root, ec);

                  auto importer
                    = std::make_unique<content::import::AssetImporter>();

                  try {
                    content::import::ImportRequest request {};
                    request.source_path = p;
                    request.cooked_root = cooked_root;
                    request.options.naming_strategy = std::make_shared<
                      content::import::NormalizeNamingStrategy>();

                    (void)importer->ImportToLooseCooked(request);

                    auto inspection
                      = std::make_unique<content::LooseCookedInspection>();
                    const auto index_path = cooked_root
                      / request.loose_cooked_layout.index_file_name;
                    inspection->LoadFromFile(index_path);

                    const auto expected_scene_name = p.stem().string();
                    const auto expected_virtual_path
                      = request.loose_cooked_layout.SceneVirtualPath(
                        expected_scene_name);

                    std::optional<data::AssetKey> matching_scene_key;
                    std::optional<data::AssetKey> first_scene_key;
                    std::string first_scene_path;
                    for (const auto& a : inspection->Assets()) {
                      if (a.asset_type
                        != static_cast<uint8_t>(data::AssetType::kScene)) {
                        continue;
                      }
                      if (a.virtual_path == expected_virtual_path) {
                        matching_scene_key = a.key;
                      }
                      if (!first_scene_key
                        || a.virtual_path < first_scene_path) {
                        first_scene_key = a.key;
                        first_scene_path = a.virtual_path;
                      }
                    }
                    return matching_scene_key ? matching_scene_key
                                              : first_scene_key;

                  } catch (const std::exception& e) {
                    LOG_F(ERROR, "RenderScene: FBX cook failed: {}", e.what());
                    return std::nullopt;
                  }
                });
            }
          }
          ImGui::EndListBox();
        }
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("PAK")) {
#if defined(OXYGEN_WINDOWS)
      if (ImGui::Button("Pick PAK...")) {
        std::string chosen;
        if (TryBrowseForPakFile(chosen)) {
          std::snprintf(
            pak_path_.data(), pak_path_.size(), "%s", chosen.c_str());
          pending_mount_pak_ = true;
        }
      }

      if (ImGui::BeginListBox(
            "Scenes##Pak", ImVec2(kScenesListWidth, kScenesListHeight))) {
        for (int i = 0; i < static_cast<int>(pak_scenes_.size()); ++i) {
          const auto& s = pak_scenes_[static_cast<size_t>(i)];
          if (ImGui::Selectable(s.virtual_path.c_str(), false)) {
            pending_scene_key_ = s.key;
            pending_load_scene_ = true;
          }
        }
        ImGui::EndListBox();
      }
#else
      ImGui::TextUnformatted("PAK picking is only supported on Windows.");
#endif
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Loose Cooked")) {
#if defined(OXYGEN_WINDOWS)
      if (ImGui::Button("Pick Index...")) {
        std::string chosen;
        if (TryBrowseForLooseCookedIndexFile(chosen)) {
          std::snprintf(loose_index_path_.data(), loose_index_path_.size(),
            "%s", chosen.c_str());
          pending_load_loose_index_ = true;
        }
      }

      if (ImGui::BeginListBox(
            "Scenes##Loose", ImVec2(kScenesListWidth, kScenesListHeight))) {
        for (int i = 0; i < static_cast<int>(loose_scenes_.size()); ++i) {
          const auto& s = loose_scenes_[static_cast<size_t>(i)];
          if (ImGui::Selectable(s.virtual_path.c_str(), false)) {
            pending_scene_key_ = s.key;
            pending_load_scene_ = true;
          }
        }
        ImGui::EndListBox();
      }
#else
      ImGui::TextUnformatted(
        "Loose cooked index picking is only supported on Windows.");
#endif
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

auto MainModule::DrawCameraControls(engine::FrameContext& /*context*/) -> void
{
  ImGui::SetNextWindowPos(ImVec2(550, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Camera Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Camera / Axis Debug");

  ImGui::Separator();
  ImGui::TextUnformatted("Input Debug");
  {
    const auto& io = ImGui::GetIO();
    ImGui::Text("ImGui WantCaptureKeyboard: %s",
      io.WantCaptureKeyboard ? "true" : "false");
    ImGui::Text(
      "ImGui WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");

    auto action_state
      = [](const std::shared_ptr<oxygen::input::Action>& a) -> const char* {
      if (!a) {
        return "<null>";
      }
      if (a->WasCanceledThisFrame()) {
        return "Canceled";
      }
      if (a->WasCompletedThisFrame()) {
        return "Completed";
      }
      if (a->WasTriggeredThisFrame()) {
        return "Triggered";
      }
      if (a->WasReleasedThisFrame()) {
        return "Released";
      }
      if (a->IsOngoing()) {
        return "Ongoing";
      }
      if (a->WasValueUpdatedThisFrame()) {
        return "Updated";
      }
      return "Idle";
    };

    auto show_bool = [&](const char* label,
                       const std::shared_ptr<oxygen::input::Action>& a) {
      ImGui::Text("%-10s  state=%-9s ongoing=%d trig=%d rel=%d", label,
        action_state(a), (a && a->IsOngoing()) ? 1 : 0,
        (a && a->WasTriggeredThisFrame()) ? 1 : 0,
        (a && a->WasReleasedThisFrame()) ? 1 : 0);
    };

    show_bool("W", move_fwd_action_);
    show_bool("S", move_bwd_action_);
    show_bool("A", move_left_action_);
    show_bool("D", move_right_action_);
    show_bool("Shift", fly_boost_action_);
    show_bool("Space", fly_plane_lock_action_);
    show_bool("RMB", rmb_action_);

    // MouseXY delta accumulation (same pattern used for orbit/look).
    glm::vec2 mouse_delta(0.0f);
    if (orbit_action_
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        mouse_delta.x += v.x;
        mouse_delta.y += v.y;
      }
    }
    ImGui::Text("MouseXY delta: (%.3f, %.3f)", mouse_delta.x, mouse_delta.y);
  }

  {
    int mode_ui = (camera_mode_ == CameraMode::kOrbit) ? 0 : 1;
    if (ImGui::Combo("Camera Mode", &mode_ui, "Orbit\0Fly\0")) {
      camera_mode_ = (mode_ui == 0) ? CameraMode::kOrbit : CameraMode::kFly;

      // Swap input contexts immediately (user expectation: orbit mappings in
      // orbit mode, fly mappings in fly mode).
      UpdateActiveCameraInputContext();

      pending_sync_active_camera_ = true;
    }
  }

  if (camera_mode_ == CameraMode::kOrbit && orbit_controller_) {
    int orbit_mode_ui
      = (orbit_controller_->GetMode() == OrbitMode::kTrackball) ? 0 : 1;
    if (ImGui::Combo("Orbit Mode", &orbit_mode_ui, "Trackball\0Turntable\0")) {
      orbit_controller_->SetMode(
        (orbit_mode_ui == 0) ? OrbitMode::kTrackball : OrbitMode::kTurntable);
      orbit_controller_->SyncFromTransform(active_camera_);
    }
  } else if (camera_mode_ == CameraMode::kFly && fly_controller_) {
    ImGui::Text("Fly Speed: %.2f", fly_controller_->GetMoveSpeed());
  }

  if (!active_camera_.IsAlive()) {
    ImGui::TextUnformatted("Active camera: <none>");
  } else {
    auto tf = active_camera_.GetTransform();
    glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
    glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };

    if (auto lp = tf.GetLocalPosition()) {
      cam_pos = *lp;
    }
    if (auto lr = tf.GetLocalRotation()) {
      cam_rot = *lr;
    }

    const glm::vec3 forward = cam_rot * glm::vec3(0.0F, 0.0F, -1.0F);
    const glm::vec3 up = cam_rot * glm::vec3(0.0F, 1.0F, 0.0F);
    const glm::vec3 right = cam_rot * glm::vec3(1.0F, 0.0F, 0.0F);

    ImGui::Text(
      "cam_pos:   (%.3f, %.3f, %.3f)", cam_pos.x, cam_pos.y, cam_pos.z);
    ImGui::Text(
      "forward:   (%.3f, %.3f, %.3f)", forward.x, forward.y, forward.z);
    ImGui::Text("up:        (%.3f, %.3f, %.3f)", up.x, up.y, up.z);
    ImGui::Text("right:     (%.3f, %.3f, %.3f)", right.x, right.y, right.z);

    const auto safe_norm = [](const glm::vec3& v) -> glm::vec3 {
      const float len2 = glm::dot(v, v);
      if (len2 <= 0.0F) {
        return glm::vec3(0.0F);
      }
      return v / std::sqrt(len2);
    };

    const glm::vec3 fwd_n = safe_norm(forward);
    const glm::vec3 up_n = safe_norm(up);

    const glm::vec3 world_pos_y(0.0F, 1.0F, 0.0F);
    const glm::vec3 world_neg_y(0.0F, -1.0F, 0.0F);
    const glm::vec3 world_pos_z(0.0F, 0.0F, 1.0F);

    ImGui::Text("dot(fwd,+Y)=%.3f  dot(fwd,-Y)=%.3f",
      glm::dot(fwd_n, world_pos_y), glm::dot(fwd_n, world_neg_y));
    ImGui::Text(
      "dot(up,+Z)=%.3f (expect ~1.0 if Z-up)", glm::dot(up_n, world_pos_z));

    ImGui::Text("world up (+Z): (0.0, 0.0, 1.0)");
  }

  ImGui::End();
}

} // namespace oxygen::examples::render_scene
