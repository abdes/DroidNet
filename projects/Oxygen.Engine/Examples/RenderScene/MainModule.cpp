//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
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
  const auto right = glm::normalize(glm::cross(forward, up_direction));
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(look_matrix);
}

} // namespace

namespace oxygen::examples::render_scene {

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
    using oxygen::data::pak::NodeRecord;
    using oxygen::data::pak::OrthographicCameraRecord;
    using oxygen::data::pak::PerspectiveCameraRecord;
    using oxygen::data::pak::RenderableRecord;

    const auto nodes = asset->GetNodes();
    runtime_nodes_.reserve(nodes.size());

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

    // Pick or create an active camera.
    const auto perspective_cams
      = asset->GetComponents<PerspectiveCameraRecord>();
    if (!perspective_cams.empty()) {
      const auto& rec = perspective_cams.front();
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index < runtime_nodes_.size()) {
        swap_.active_camera = runtime_nodes_[node_index];
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
          cam.SetFieldOfView(rec.fov_y);
          cam.SetNearPlane(rec.near_plane);
          cam.SetFarPlane(rec.far_plane);
        }
      }
    }

    // If no perspective, try ortho
    if (!swap_.active_camera.IsAlive()) {
      const auto ortho_cams = asset->GetComponents<OrthographicCameraRecord>();
      if (!ortho_cams.empty()) {
        const auto& rec = ortho_cams.front();
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index < runtime_nodes_.size()) {
          swap_.active_camera = runtime_nodes_[node_index];
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
            cam_ref->get().SetExtents(rec.left, rec.right, rec.bottom, rec.top,
              rec.near_plane, rec.far_plane);
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
      swap_.active_camera.GetTransform().SetLocalPosition(
        glm::vec3(0.0F, 0.0F, 5.0F));
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

  UpdateFrameContext(context, [this](int w, int h) {
    last_viewport_w_ = w;
    last_viewport_h_ = h;
    EnsureActiveCameraViewport(w, h);
    ApplyOrbitAndZoom();
    EnsureViewCameraRegistered();
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

auto MainModule::OnGameplay(engine::FrameContext& /*context*/) -> co::Co<>
{
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(WARNING, "RenderScene: OnGameplay is running");
  }

  // Keep camera updates in scene mutation for immediate transform propagation.
  co_return;
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
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

  app_.input_system->AddAction(zoom_in_action_);
  app_.input_system->AddAction(zoom_out_action_);
  app_.input_system->AddAction(rmb_action_);
  app_.input_system->AddAction(orbit_action_);

  LOG_F(WARNING, "RenderScene: Added actions (zoom_in/zoom_out/rmb/orbit)");

  camera_controls_ctx_ = std::make_shared<InputMappingContext>("camera");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // RMB helper mapping
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      camera_controls_ctx_->AddMapping(mapping);
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
      camera_controls_ctx_->AddMapping(mapping);
    }

    app_.input_system->AddMappingContext(camera_controls_ctx_, 10);
    app_.input_system->ActivateMappingContext(camera_controls_ctx_);
  }

  LOG_F(WARNING,
    "RenderScene: Activated mapping context 'camera' (priority={})", 10);

  return true;
}

auto MainModule::ApplyOrbitAndZoom() -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  const auto camera_handle = active_camera_.GetHandle();
  if (orbit_camera_ != camera_handle) {
    orbit_camera_ = camera_handle;
    SyncOrbitFromActiveCamera();
  }

  // Zoom via mouse wheel actions
  if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::max)(orbit_distance_ - zoom_step_, min_cam_distance_);
    LOG_F(
      WARNING, "RenderScene: Zoom in -> orbit_distance={}", orbit_distance_);
  }
  if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::min)(orbit_distance_ + zoom_step_, max_cam_distance_);
    LOG_F(
      WARNING, "RenderScene: Zoom out -> orbit_distance={}", orbit_distance_);
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
      if (!was_orbiting_last_frame_) {
        LOG_F(WARNING, "RenderScene: Orbit start (delta_x={} delta_y={})",
          orbit_delta.x, orbit_delta.y);
      }

      orbit_yaw_rad_ += orbit_delta.x * orbit_sensitivity_;
      orbit_pitch_rad_ += orbit_delta.y * orbit_sensitivity_ * -1.0f;

      const float kMinPitch = -glm::half_pi<float>() + 0.05f;
      const float kMaxPitch = glm::half_pi<float>() - 0.05f;
      orbit_pitch_rad_ = std::clamp(orbit_pitch_rad_, kMinPitch, kMaxPitch);

      was_orbiting_last_frame_ = true;
    } else {
      was_orbiting_last_frame_ = false;
    }
  } else {
    was_orbiting_last_frame_ = false;
  }

  const float cp = std::cos(orbit_pitch_rad_);
  const float sp = std::sin(orbit_pitch_rad_);
  const float cy = std::cos(orbit_yaw_rad_);
  const float sy = std::sin(orbit_yaw_rad_);

  const glm::vec3 offset = orbit_distance_ * glm::vec3(cp * cy, cp * sy, sp);
  const glm::vec3 cam_pos = camera_target_ + offset;

  auto tf = active_camera_.GetTransform();
  tf.SetLocalPosition(cam_pos);
  tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, camera_target_));
}

auto MainModule::SyncOrbitFromActiveCamera() -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  auto tf = active_camera_.GetTransform();
  const auto cam_pos_opt = tf.GetLocalPosition();
  const auto cam_rot_opt = tf.GetLocalRotation();
  if (!cam_pos_opt || !cam_rot_opt) {
    return;
  }

  const auto cam_pos = *cam_pos_opt;
  const auto cam_rot = *cam_rot_opt;

  // Orbit target: keep orbiting around the scene center (origin).
  // We only sync the controller's yaw/pitch/distance from the camera pose.
  camera_target_ = glm::vec3(0.0f, 0.0f, 0.0f);

  const glm::vec3 offset = cam_pos - camera_target_;
  const float dist_len2 = glm::dot(offset, offset);
  if (dist_len2 <= 1e-8f) {
    orbit_distance_ = 6.0f;
    orbit_yaw_rad_ = -glm::half_pi<float>();
    orbit_pitch_rad_ = 0.0f;
    was_orbiting_last_frame_ = false;
    return;
  }

  const float distance
    = std::clamp(std::sqrt(dist_len2), min_cam_distance_, max_cam_distance_);
  orbit_distance_ = distance;
  const glm::vec3 dir = offset / distance;

  orbit_pitch_rad_ = std::asin(std::clamp(dir.z, -1.0f, 1.0f));
  orbit_yaw_rad_ = std::atan2(dir.y, dir.x);
  was_orbiting_last_frame_ = false;
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

  if (auto imgui_module_ref = app_.engine
      ? app_.engine->GetModule<imgui::ImGuiModule>()
      : std::nullopt) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  DrawDebugOverlay(context);
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
    active_camera_.GetTransform().SetLocalPosition(glm::vec3(0.0F, 0.0F, 5.0F));
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

} // namespace oxygen::examples::render_scene
