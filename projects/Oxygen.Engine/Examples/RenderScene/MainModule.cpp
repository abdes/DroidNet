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
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
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
#include <Oxygen/Scene/Types/Flags.h>

using oxygen::scene::SceneNodeFlags;

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace {

auto BuildFloorPlaneGeometry() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::Vertex;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  constexpr unsigned int kSegments = 32;
  constexpr float kSize = 10.0F;

  auto plane_data
    = oxygen::data::MakePlaneMeshAsset(kSegments, kSegments, kSize);
  if (!plane_data) {
    return nullptr;
  }

  std::vector<Vertex> vertices = std::move(plane_data->first);
  std::vector<uint32_t> indices = std::move(plane_data->second);

  // Ensure the plane is visible from both sides under backface culling by
  // adding a second copy of every triangle with reversed winding.
  const auto original_index_count = indices.size();
  indices.reserve(original_index_count * 2);
  for (std::size_t i = 0; i + 2 < original_index_count; i += 3) {
    indices.push_back(indices[i]);
    indices.push_back(indices[i + 2]);
    indices.push_back(indices[i + 1]);
  }

  auto mesh = MeshBuilder(0, "FloorPlaneLOD0")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("default", MaterialAsset::CreateDefault())
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(indices.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc,
    std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });
}

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

    LOG_F(INFO,
      "SceneLoader: Scene summary: nodes={} renderables={} "
      "perspective_cameras={} orthographic_cameras={}",
      nodes.size(), asset->GetComponents<RenderableRecord>().size(),
      asset->GetComponents<PerspectiveCameraRecord>().size(),
      asset->GetComponents<OrthographicCameraRecord>().size());

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

    // Minimal floor plane: root node at origin.
    if (auto floor_geo = BuildFloorPlaneGeometry(); floor_geo) {
      auto floor = swap_.scene->CreateNode("Floor");
      auto tf = floor.GetTransform();
      tf.SetLocalPosition(glm::vec3(0.0F, 0.0F, 0.0F));
      tf.SetLocalRotation(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
      tf.SetLocalScale(glm::vec3(1.0F, 1.0F, 1.0F));
      floor.GetRenderable().SetGeometry(std::move(floor_geo));
    } else {
      LOG_F(WARNING, "SceneLoader: Failed to build floor plane geometry");
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
      // Stable, non-singular pose: look along Y with Z-up.
      const glm::vec3 cam_pos(0.0F, 5.0F, 0.0F);
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

  if (pending_fbx_import_path_) {
    const auto fbx_path = std::move(*pending_fbx_import_path_);
    pending_fbx_import_path_.reset();

    const auto cooked_root
      = std::filesystem::absolute(content_root_ / ".cooked");
    std::error_code ec;
    (void)std::filesystem::create_directories(cooked_root, ec);

    if (!asset_importer_) {
      asset_importer_ = std::make_unique<content::import::AssetImporter>();
    }

    try {
      content::import::ImportRequest request {};
      request.source_path = fbx_path;
      request.cooked_root = cooked_root;
      request.options.naming_strategy
        = std::make_shared<content::import::NormalizeNamingStrategy>();

      (void)asset_importer_->ImportToLooseCooked(request);

      if (!loose_inspection_) {
        loose_inspection_ = std::make_unique<content::LooseCookedInspection>();
      }

      const auto index_path
        = cooked_root / request.loose_cooked_layout.index_file_name;
      loose_inspection_->LoadFromFile(index_path);

      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        asset_loader->ClearMounts();
        asset_loader->AddLooseCookedRoot(cooked_root);
      }

      std::optional<data::AssetKey> first_scene_key;
      std::string first_scene_path;
      for (const auto& a : loose_inspection_->Assets()) {
        if (a.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
          continue;
        }
        if (!first_scene_key || a.virtual_path < first_scene_path) {
          first_scene_key = a.key;
          first_scene_path = a.virtual_path;
        }
      }

      if (first_scene_key) {
        pending_scene_key_ = *first_scene_key;
        pending_load_scene_ = true;
      } else {
        LOG_F(ERROR,
          "RenderScene: Cooked FBX produced no scene assets (fbx={})",
          fbx_path.string());
      }
    } catch (const std::exception& e) {
      LOG_F(ERROR, "RenderScene: FBX cook failed: {}", e.what());
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

    // Orbit control assumes the camera transform is in a stable (root) space.
    // Imported cameras may be parented under animated or offset hierarchies;
    // in that case, treating the camera's local axes as world axes causes
    // orbit drift and eventually mixes dx/dy behavior.
    if (scene_ && !active_camera_.IsRoot()) {
      const bool ok = scene_->MakeNodeRoot(active_camera_);
      if (!ok) {
        LOG_F(WARNING,
          "RenderScene: Failed to make active camera a root node; orbit may "
          "feel unstable");
      }
    }

    SyncOrbitFromActiveCamera();
    SyncTurntableFromActiveCamera();
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

  // Keep the local offset consistent with distance.
  // Camera forward is -Z (see MakeLookRotationFromPosition), so the vector from
  // target to camera is +Z in camera local space.
  orbit_offset_local_ = glm::vec3(0.0f, 0.0f, orbit_distance_);

  glm::vec2 orbit_delta(0.0F);
  bool has_orbit_delta = false;

  // Orbit via MouseXY deltas for this frame
  if (orbit_action_
    && orbit_action_->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
    for (const auto& tr : orbit_action_->GetFrameTransitions()) {
      const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
      orbit_delta.x += v.x;
      orbit_delta.y += v.y;
    }

    if (std::abs(orbit_delta.x) > 0.0f || std::abs(orbit_delta.y) > 0.0f) {
      has_orbit_delta = true;
      if (!was_orbiting_last_frame_) {
        LOG_F(WARNING, "RenderScene: Orbit start (delta_x={} delta_y={})",
          orbit_delta.x, orbit_delta.y);
      }

      was_orbiting_last_frame_ = true;
    } else {
      was_orbiting_last_frame_ = false;
    }
  } else {
    was_orbiting_last_frame_ = false;
  }

  // Trackball orbit for a camera (viewport navigation): rotate around the
  // *current* view axes so horizontal mouse motion stays purely horizontal on
  // screen, and vertical stays purely vertical. This matches the expected
  // behavior for trackball view orbit.
  //
  // "Iron Orbit" logic: The camera is rigidly locked to a sphere (orbit) and
  // slides along it based on screen-space input. We do NOT constrain the
  // up-vector, allowing the camera to pass the pole (and become upside down)
  // without any singularity or forced roll.
  if (has_orbit_delta) {
    if (orbit_mode_ == OrbitMode::kTrackball) {
      // Blender-style trackball (see Blender's `transform_mode_trackball.cc`):
      // Compute a rotation vector (axis * angle) in WORLD space as a linear
      // combination of the current view X/Y axes.
      //
      // Mouse mapping parity with Blender's INPUT_TRACKBALL:
      //   phi[0] ~= (start_y - current_y) * factor  -> -dy
      //   phi[1] ~= (current_x - start_x) * factor  -> +dx
      //
      // Then:
      //   rot_vec = view_x * phi[0] + view_y * phi[1]
      //   angle   = |rot_vec|
      //   axis    = rot_vec / angle
      //   orbit_rot = angleAxis(angle, axis) * orbit_rot
      const float phi0 = -orbit_delta.y * orbit_sensitivity_;
      const float phi1 = orbit_delta.x * orbit_sensitivity_;

      const glm::vec3 view_x_ws
        = glm::normalize(orbit_rot_ * glm::vec3(1.0F, 0.0F, 0.0F));
      const glm::vec3 view_y_ws
        = glm::normalize(orbit_rot_ * glm::vec3(0.0F, 1.0F, 0.0F));

      const glm::vec3 rot_vec_ws = (view_x_ws * phi0) + (view_y_ws * phi1);
      const float angle = glm::length(rot_vec_ws);
      if (angle > 1e-8F) {
        const glm::vec3 axis_ws = rot_vec_ws / angle;
        const glm::quat delta = glm::angleAxis(angle, axis_ws);
        orbit_rot_ = glm::normalize(delta * orbit_rot_);
      }
    } else {
      // Turntable: yaw around world-up (Z) and pitch around the derived right.
      // We keep the horizon level by rebuilding orientation from position.
      const float yaw_step = orbit_delta.x * orbit_sensitivity_;
      const float pitch_step = orbit_delta.y * orbit_sensitivity_;

      // Blender parity: when upside down, reverse yaw direction so mouse-left
      // still yaws left on screen.
      turntable_yaw_ += turntable_inverted_ ? -yaw_step : yaw_step;

      // To keep turntable stable and horizon-locked across the poles, keep the
      // stored pitch in [-pi/2, +pi/2] and "wrap" across poles by reflecting
      // pitch and rotating yaw by pi (same camera position on the sphere).
      //
      // When inverted, the pitch accumulator must run in the opposite
      // direction to keep mouse dy mapping continuous.
      const float signed_pitch_step
        = turntable_inverted_ ? -pitch_step : pitch_step;
      turntable_pitch_ += signed_pitch_step;

      constexpr float kPi = std::numbers::pi_v<float>;
      constexpr float kHalfPi = 0.5F * std::numbers::pi_v<float>;
      constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
      constexpr float kPoleEpsilon = 1e-5F;

      while (turntable_pitch_ > kHalfPi) {
        turntable_pitch_ = kPi - turntable_pitch_;
        turntable_yaw_ += kPi;
        turntable_inverted_ = !turntable_inverted_;
      }
      while (turntable_pitch_ < -kHalfPi) {
        turntable_pitch_ = -kPi - turntable_pitch_;
        turntable_yaw_ += kPi;
        turntable_inverted_ = !turntable_inverted_;
      }

      // Avoid exact poles where yaw is undefined.
      turntable_pitch_ = std::clamp(
        turntable_pitch_, -kHalfPi + kPoleEpsilon, kHalfPi - kPoleEpsilon);

      // Keep yaw bounded to avoid precision loss.
      turntable_yaw_ = std::remainder(turntable_yaw_, kTwoPi);
    }
  }

  glm::vec3 cam_pos(0.0F);
  if (orbit_mode_ == OrbitMode::kTurntable) {
    const float cos_pitch = std::cos(turntable_pitch_);
    const float sin_pitch = std::sin(turntable_pitch_);
    const float cos_yaw = std::cos(turntable_yaw_);
    const float sin_yaw = std::sin(turntable_yaw_);

    const glm::vec3 dir_ws(sin_yaw * cos_pitch, cos_yaw * cos_pitch, sin_pitch);

    cam_pos = camera_target_ + (dir_ws * orbit_distance_);

    const glm::vec3 world_up(0.0F, 0.0F, turntable_inverted_ ? -1.0F : 1.0F);
    const glm::vec3 forward_ws = glm::normalize(camera_target_ - cam_pos);

    glm::vec3 right_ws = glm::cross(forward_ws, world_up);
    const float right_len2 = glm::dot(right_ws, right_ws);
    if (right_len2 <= 1e-8F) {
      // At/near the poles `world_up` is parallel to `forward_ws`.
      // Use yaw to define a stable horizon-locked right axis.
      // For yaw=0 (looking from +Y), Right should be -X (if world-up is +Z).
      const float sign = turntable_inverted_ ? 1.0F : -1.0F;
      right_ws
        = glm::normalize(glm::vec3(sign * cos_yaw, -sign * sin_yaw, 0.0F));
    } else {
      right_ws = right_ws / std::sqrt(right_len2);
    }
    const glm::vec3 up_ws = glm::cross(right_ws, forward_ws);

    glm::mat4 view_basis(1.0F);
    view_basis[0] = glm::vec4(right_ws, 0.0F);
    view_basis[1] = glm::vec4(up_ws, 0.0F);
    view_basis[2] = glm::vec4(-forward_ws, 0.0F);
    orbit_rot_ = glm::normalize(glm::quat_cast(view_basis));
  } else {
    cam_pos = camera_target_ + (orbit_rot_ * orbit_offset_local_);
  }

  auto tf = active_camera_.GetTransform();

  constexpr float kPosEpsilon = 1e-6F;
  constexpr float kRotDotEpsilon = 1e-6F;

  const auto current_pos_opt = tf.GetLocalPosition();
  const auto current_rot_opt = tf.GetLocalRotation();

  auto new_rot = orbit_rot_;

  bool position_changed = true;
  if (current_pos_opt) {
    const glm::vec3 delta = *current_pos_opt - cam_pos;
    position_changed = glm::dot(delta, delta) > (kPosEpsilon * kPosEpsilon);
  }

  bool rotation_changed = true;
  if (current_rot_opt) {
    const float dot = std::abs(glm::dot(*current_rot_opt, new_rot));
    rotation_changed = dot < (1.0F - kRotDotEpsilon);
  }

  // Avoid re-writing identical TRS every frame; setters mark transforms dirty
  // and will cause scene traversal to recompute world transforms.
  if (position_changed) {
    tf.SetLocalPosition(cam_pos);
  }
  if (rotation_changed) {
    tf.SetLocalRotation(new_rot);
  }
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
    orbit_rot_ = glm::normalize(MakeLookRotationFromPosition(
      glm::vec3(0.0F, orbit_distance_, 0.0F), camera_target_));
    orbit_offset_local_ = glm::vec3(0.0f, 0.0f, orbit_distance_);
    was_orbiting_last_frame_ = false;
    return;
  }

  const float distance
    = std::clamp(std::sqrt(dist_len2), min_cam_distance_, max_cam_distance_);
  orbit_distance_ = distance;

  orbit_rot_ = glm::normalize(cam_rot);
  orbit_offset_local_ = glm::vec3(0.0f, 0.0f, orbit_distance_);

  was_orbiting_last_frame_ = false;
}

auto MainModule::SyncTurntableFromActiveCamera() -> void
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

  const glm::vec3 cam_pos = *cam_pos_opt;
  const glm::quat cam_rot = *cam_rot_opt;

  const glm::vec3 offset = cam_pos - camera_target_;
  const float dist_len2 = glm::dot(offset, offset);
  if (dist_len2 <= 1e-8F) {
    turntable_yaw_ = 0.0F;
    turntable_pitch_ = 0.0F;
    return;
  }

  const float inv_dist = 1.0F / std::sqrt(dist_len2);
  const glm::vec3 dir_ws = offset * inv_dist;

  const glm::vec3 cam_up_ws
    = glm::normalize(cam_rot * glm::vec3(0.0F, 1.0F, 0.0F));
  turntable_inverted_ = glm::dot(cam_up_ws, glm::vec3(0.0F, 0.0F, 1.0F)) < 0.0F;

  constexpr float kPi = std::numbers::pi_v<float>;
  constexpr float kHalfPi = 0.5F * std::numbers::pi_v<float>;
  constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
  constexpr float kPoleEpsilon = 1e-5F;

  const float yaw = std::atan2(dir_ws.x, dir_ws.y);

  const float xy_len = std::sqrt((dir_ws.x * dir_ws.x) + (dir_ws.y * dir_ws.y));
  const float pitch_basic = std::atan2(dir_ws.z, xy_len);

  turntable_yaw_ = std::remainder(yaw, kTwoPi);
  turntable_pitch_
    = std::clamp(pitch_basic, -kHalfPi + kPoleEpsilon, kHalfPi - kPoleEpsilon);
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

    // Start with a stable, non-singular pose: look along the Y axis with Z-up.
    // This makes it unambiguous whether imported assets are rotated.
    const glm::vec3 cam_pos(0.0F, 5.0F, 0.0F);
    const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));
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
      const auto fbx_dir = content_root_ / "fbx";
      const auto files = EnumerateFbxFiles(fbx_dir);

      if (ImGui::BeginListBox(
            "FBX##Fbx", ImVec2(kScenesListWidth, kScenesListHeight))) {
        for (const auto& p : files) {
          const auto name = p.filename().string();
          if (ImGui::Selectable(name.c_str(), false)) {
            pending_fbx_import_path_ = p;
          }
        }
        ImGui::EndListBox();
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

  ImGui::Separator();
  ImGui::TextUnformatted("Camera / Axis Debug");

  int orbit_mode_ui = (orbit_mode_ == OrbitMode::kTrackball) ? 0 : 1;
  if (ImGui::Combo("Orbit Mode", &orbit_mode_ui, "Trackball\0Turntable\0")) {
    orbit_mode_
      = (orbit_mode_ui == 0) ? OrbitMode::kTrackball : OrbitMode::kTurntable;
    SyncOrbitFromActiveCamera();
    SyncTurntableFromActiveCamera();
    was_orbiting_last_frame_ = false;
  }

  if (!active_camera_.IsAlive()) {
    ImGui::TextUnformatted("Active camera: <none>");
  } else {
    auto tf = active_camera_.GetTransform();
    glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
    glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };

    if (auto wp = tf.GetWorldPosition()) {
      cam_pos = *wp;
    } else if (auto lp = tf.GetLocalPosition()) {
      cam_pos = *lp;
    }
    if (auto wr = tf.GetWorldRotation()) {
      cam_rot = *wr;
    } else if (auto lr = tf.GetLocalRotation()) {
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
