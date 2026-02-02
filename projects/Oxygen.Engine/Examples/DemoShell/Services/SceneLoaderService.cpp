//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/EnvironmentHydrator.h"
#include "DemoShell/Services/SceneLoaderService.h"

namespace oxygen::examples {

namespace {

  constexpr std::string_view kSceneName = "RenderScene";

  auto MakeNodeName(std::string_view name_view, const size_t index)
    -> std::string
  {
    if (name_view.empty()) {
      return "Node" + std::to_string(index);
    }
    return std::string(name_view);
  }

  auto MakeLookRotationFromPosition(const glm::vec3& position,
    const glm::vec3& target,
    const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F }) -> glm::quat
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

} // namespace

SceneLoaderService::SceneLoaderService(
  content::IAssetLoader& loader, const int width, const int height)
  : loader_(loader)
  , width_(width)
  , height_(height)
{
}

SceneLoaderService::~SceneLoaderService()
{
  // Ensure any geometry pins are released if the loader is torn down early.
  ReleasePinnedGeometryAssets();
  LOG_F(INFO, "SceneLoader: Destroying loader.");
}

void SceneLoaderService::StartLoad(const data::AssetKey& key)
{
  LOG_F(INFO, "SceneLoader: Starting load for scene key: {}",
    oxygen::data::to_string(key));

  swap_.scene_key = key;
  // Start loading the scene asset
  loader_.StartLoadScene(key,
    [weak_self = weak_from_this()](std::shared_ptr<data::SceneAsset> asset) {
      if (auto self = weak_self.lock()) {
        self->OnSceneLoaded(std::move(asset));
      }
    });
}

void SceneLoaderService::MarkConsumed()
{
  consumed_ = true;
  swap_.asset.reset();
  runtime_nodes_.clear();
  active_camera_ = {};
  // Drop any pins that were never released due to early consumption.
  ReleasePinnedGeometryAssets();
  linger_frames_ = 2;
}

auto SceneLoaderService::Tick() -> bool
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

void SceneLoaderService::OnSceneLoaded(std::shared_ptr<data::SceneAsset> asset)
{
  try {
    if (!asset) {
      LOG_F(ERROR, "SceneLoader: Failed to load scene asset");
      failed_ = true;
      return;
    }

    LOG_F(INFO, "SceneLoader: Scene asset loaded. Ready to instantiate.");

    runtime_nodes_.clear();
    active_camera_ = {};
    swap_.asset = std::move(asset);
    // Block readiness until geometry dependencies are pinned to avoid
    // evictions during rapid scene swaps.
    ready_ = false;
    failed_ = false;
    QueueGeometryDependencies(*swap_.asset);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "SceneLoader: Exception while building scene: {}", ex.what());
    swap_ = {};
    runtime_nodes_.clear();
    active_camera_ = {};
    ready_ = false;
    failed_ = true;
  } catch (...) {
    LOG_F(ERROR, "SceneLoader: Unknown exception while building scene");
    swap_ = {};
    runtime_nodes_.clear();
    active_camera_ = {};
    ready_ = false;
    failed_ = true;
  }
}

/*!
 Prime geometry dependencies while the scene asset is pending instantiation.

 This pins geometry assets by issuing load requests and keeping the
 loader references alive until `BuildScene()` completes. It prevents
 rapid swaps from evicting geometry between dependency resolution and
 attachment.

 @param asset Scene asset providing renderable records.

 ### Performance Characteristics

 - Time Complexity: $O(n)$ over renderables.
 - Memory: $O(n)$ for key bookkeeping.
 - Optimization: Deduplicates keys before issuing async loads.

 @note Readiness is only reported once all geometry dependencies have
 either loaded or failed.
*/
void SceneLoaderService::QueueGeometryDependencies(
  const data::SceneAsset& asset)
{
  ReleasePinnedGeometryAssets();

  pending_geometry_keys_.clear();

  for (const auto& renderable :
    asset.GetComponents<data::pak::RenderableRecord>()) {
    pending_geometry_keys_.insert(renderable.geometry_key);
  }

  if (pending_geometry_keys_.empty()) {
    ready_ = true;
    return;
  }

  for (const auto& geom_key : pending_geometry_keys_) {
    loader_.StartLoadGeometryAsset(geom_key,
      [weak_self = weak_from_this(), geom_key](
        std::shared_ptr<data::GeometryAsset> geom) {
        if (auto self = weak_self.lock()) {
          const auto it = self->pending_geometry_keys_.find(geom_key);
          if (it == self->pending_geometry_keys_.end()) {
            return;
          }

          self->pending_geometry_keys_.erase(it);
          if (geom) {
            self->pinned_geometry_keys_.push_back(geom_key);
          } else {
            LOG_F(WARNING, "SceneLoader: Failed to load geometry dependency {}",
              oxygen::data::to_string(geom_key));
          }

          if (self->pending_geometry_keys_.empty()) {
            self->ready_ = true;
          }
        }
      });
  }
}

/*!
 Release loader-held geometry references after scene instantiation.

 Geometry assets are pinned only for the narrow window between scene load
 completion and runtime scene construction. Releasing here restores
 normal cache eviction behavior without leaving stale loader references
 behind.

 ### Performance Characteristics

 - Time Complexity: $O(n)$ over pinned geometry keys.
 - Memory: Releases pin bookkeeping.
 - Optimization: Early-out when nothing is pinned.
*/
void SceneLoaderService::ReleasePinnedGeometryAssets()
{
  if (pinned_geometry_keys_.empty()) {
    pending_geometry_keys_.clear();
    return;
  }

  for (const auto& key : pinned_geometry_keys_) {
    (void)loader_.ReleaseAsset(key);
  }
  pinned_geometry_keys_.clear();
  pending_geometry_keys_.clear();
}

auto SceneLoaderService::BuildScene(
  scene::Scene& scene, const data::SceneAsset& asset) -> scene::SceneNode
{
  LOG_F(INFO, "SceneLoader: Instantiating runtime scene '{}'", kSceneName);

  runtime_nodes_.clear();
  active_camera_ = {};

  scene.SetEnvironment(BuildEnvironment(asset));
  LogSceneSummary(asset);
  InstantiateNodes(scene, asset);
  ApplyHierarchy(scene, asset);
  AttachRenderables(asset);
  AttachLights(asset);
  SelectActiveCamera(asset);
  EnsureCameraAndViewport(scene);
  // Geometry pins are only needed until scene instantiation finishes.
  ReleasePinnedGeometryAssets();
  LogSceneHierarchy(scene);

  LOG_F(INFO, "SceneLoader: Runtime scene instantiation complete.");
  return std::move(active_camera_);
}

auto SceneLoaderService::BuildEnvironment(const data::SceneAsset& asset)
  -> std::unique_ptr<scene::SceneEnvironment>
{
  auto environment = std::make_unique<scene::SceneEnvironment>();
  EnvironmentHydrator::HydrateEnvironment(*environment, asset);
  return environment;
}

void SceneLoaderService::LogSceneSummary(const data::SceneAsset& asset) const
{
  using data::pak::DirectionalLightRecord;
  using data::pak::OrthographicCameraRecord;
  using data::pak::PerspectiveCameraRecord;
  using data::pak::PointLightRecord;
  using data::pak::RenderableRecord;
  using data::pak::SpotLightRecord;

  const auto nodes = asset.GetNodes();

  LOG_F(INFO,
    "SceneLoader: Scene summary: nodes={} renderables={} "
    "perspective_cameras={} orthographic_cameras={} "
    "directional_lights={} point_lights={} spot_lights={}",
    nodes.size(), asset.GetComponents<RenderableRecord>().size(),
    asset.GetComponents<PerspectiveCameraRecord>().size(),
    asset.GetComponents<OrthographicCameraRecord>().size(),
    asset.GetComponents<DirectionalLightRecord>().size(),
    asset.GetComponents<PointLightRecord>().size(),
    asset.GetComponents<SpotLightRecord>().size());
}

void SceneLoaderService::InstantiateNodes(
  scene::Scene& scene, const data::SceneAsset& asset)
{
  using data::pak::NodeRecord;

  const auto nodes = asset.GetNodes();
  runtime_nodes_.reserve(nodes.size());

  for (const auto i : std::views::iota(size_t { 0 }, nodes.size())) {
    const NodeRecord& node = nodes[i];
    const std::string name = MakeNodeName(asset.GetNodeName(node), i);

    auto n = scene.CreateNode(name);
    auto tf = n.GetTransform();
    tf.SetLocalPosition(
      glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    tf.SetLocalRotation(glm::quat(
      node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]));
    tf.SetLocalScale(glm::vec3(node.scale[0], node.scale[1], node.scale[2]));

    runtime_nodes_.push_back(std::move(n));
  }
}

void SceneLoaderService::ApplyHierarchy(
  scene::Scene& scene, const data::SceneAsset& asset)
{
  const auto nodes = asset.GetNodes();

  for (const auto i : std::views::iota(size_t { 0 }, nodes.size())) {
    const auto parent_index = static_cast<size_t>(nodes[i].parent_index);
    if (parent_index == i) {
      continue;
    }
    if (parent_index >= runtime_nodes_.size()) {
      LOG_F(WARNING, "Invalid parent_index {} for node {}", parent_index, i);
      continue;
    }

    const bool ok = scene.ReparentNode(runtime_nodes_[i],
      runtime_nodes_[parent_index], /*preserve_world_transform=*/false);
    if (!ok) {
      LOG_F(WARNING, "Failed to reparent node {} under {}", i, parent_index);
    }
  }
}

void SceneLoaderService::AttachRenderables(const data::SceneAsset& asset)
{
  using data::pak::RenderableRecord;

  const auto renderables = asset.GetComponents<RenderableRecord>();
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
}

void SceneLoaderService::AttachLights(const data::SceneAsset& asset)
{
  using data::pak::DirectionalLightRecord;
  using data::pak::PointLightRecord;
  using data::pak::SpotLightRecord;

  const auto ApplyCommonLight = [](scene::CommonLightProperties& dst,
                                  const data::pak::LightCommonRecord& src) {
    dst.affects_world = (src.affects_world != 0U);
    dst.color_rgb = { src.color_rgb[0], src.color_rgb[1], src.color_rgb[2] };
    dst.intensity = src.intensity;
    dst.mobility = static_cast<scene::LightMobility>(src.mobility);
    dst.casts_shadows = (src.casts_shadows != 0U);
    dst.shadow.bias = src.shadow.bias;
    dst.shadow.normal_bias = src.shadow.normal_bias;
    dst.shadow.contact_shadows = (src.shadow.contact_shadows != 0U);
    dst.shadow.resolution_hint
      = static_cast<scene::ShadowResolutionHint>(src.shadow.resolution_hint);
    dst.exposure_compensation_ev = src.exposure_compensation_ev;
  };

  int attached_directional = 0;
  for (const DirectionalLightRecord& rec :
    asset.GetComponents<DirectionalLightRecord>()) {
    const auto node_index = static_cast<size_t>(rec.node_index);
    if (node_index >= runtime_nodes_.size()) {
      continue;
    }

    auto light = std::make_unique<scene::DirectionalLight>();
    ApplyCommonLight(light->Common(), rec.common);
    light->SetAngularSizeRadians(rec.angular_size_radians);
    light->SetEnvironmentContribution(rec.environment_contribution != 0U);
    light->SetIsSunLight(rec.is_sun_light != 0U);

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

  int attached_point = 0;
  for (const PointLightRecord& rec : asset.GetComponents<PointLightRecord>()) {
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
  for (const SpotLightRecord& rec : asset.GetComponents<SpotLightRecord>()) {
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
      LOG_F(WARNING, "SceneLoader: Failed to attach SpotLight to node_index={}",
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
}

void SceneLoaderService::SelectActiveCamera(const data::SceneAsset& asset)
{
  using data::pak::OrthographicCameraRecord;
  using data::pak::PerspectiveCameraRecord;

  const auto perspective_cams = asset.GetComponents<PerspectiveCameraRecord>();
  if (!perspective_cams.empty()) {
    LOG_F(INFO, "SceneLoader: Found {} perspective camera(s)",
      perspective_cams.size());
    const auto& rec = perspective_cams.front();
    const auto node_index = static_cast<size_t>(rec.node_index);
    if (node_index < runtime_nodes_.size()) {
      active_camera_ = runtime_nodes_[node_index];
      LOG_F(INFO,
        "SceneLoader: Using perspective camera node_index={} name='{}'",
        rec.node_index, active_camera_.GetName().c_str());
      if (!active_camera_.HasCamera()) {
        auto cam = std::make_unique<scene::PerspectiveCamera>();
        const bool attached = active_camera_.AttachCamera(std::move(cam));
        CHECK_F(
          attached, "Failed to attach PerspectiveCamera to scene camera node");
      }
      if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
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
        LOG_F(INFO,
          "SceneLoader: Camera local pose pos=({:.3F}, {:.3F}, {:.3F}) "
          "forward=({:.3F}, {:.3F}, {:.3F}) up=({:.3F}, {:.3F}, {:.3F})",
          cam_pos.x, cam_pos.y, cam_pos.z, forward.x, forward.y, forward.z,
          up.x, up.y, up.z);
      }
    }
  }

  if (!active_camera_.IsAlive()) {
    const auto ortho_cams = asset.GetComponents<OrthographicCameraRecord>();
    if (!ortho_cams.empty()) {
      LOG_F(INFO, "SceneLoader: Found {} orthographic camera(s)",
        ortho_cams.size());
      const auto& rec = ortho_cams.front();
      const auto node_index = static_cast<size_t>(rec.node_index);
      if (node_index < runtime_nodes_.size()) {
        active_camera_ = runtime_nodes_[node_index];
        LOG_F(INFO,
          "SceneLoader: Using orthographic camera node_index={} name='{}'",
          rec.node_index, active_camera_.GetName().c_str());
        if (!active_camera_.HasCamera()) {
          auto cam = std::make_unique<scene::OrthographicCamera>();
          const bool attached = active_camera_.AttachCamera(std::move(cam));
          CHECK_F(attached,
            "Failed to attach OrthographicCamera to scene camera node");
        }
        if (auto cam_ref
          = active_camera_.GetCameraAs<scene::OrthographicCamera>();
          cam_ref) {
          float near_plane = std::abs(rec.near_plane);
          float far_plane = std::abs(rec.far_plane);
          if (far_plane < near_plane) {
            std::swap(far_plane, near_plane);
          }
          cam_ref->get().SetExtents(
            rec.left, rec.right, rec.bottom, rec.top, near_plane, far_plane);
          LOG_F(INFO,
            "SceneLoader: Applied orthographic camera extents l={} r={} "
            "b={} "
            "t={} near={} far={}",
            rec.left, rec.right, rec.bottom, rec.top, near_plane, far_plane);
        }
      }
    }
  }
}

void SceneLoaderService::EnsureCameraAndViewport(scene::Scene& scene)
{
  const float aspect = height_ > 0
    ? (static_cast<float>(width_) / static_cast<float>(height_))
    : 1.0F;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width_),
    .height = static_cast<float>(height_),
    .min_depth = 0.0F,
    .max_depth = 1.0F };

  if (!active_camera_.IsAlive()) {
    active_camera_ = scene.CreateNode("MainCamera");
    constexpr glm::vec3 cam_pos(10.0F, 10.0F, 10.0F);
    constexpr glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));
    const auto handle = active_camera_.GetHandle();
    const bool already_tracked
      = std::ranges::any_of(runtime_nodes_, [&](const scene::SceneNode& node) {
          return node.IsAlive() && node.GetHandle() == handle;
        });
    if (!already_tracked) {
      runtime_nodes_.push_back(active_camera_);
    }
    LOG_F(INFO, "SceneLoader: No camera in scene; created fallback camera '{}'",
      active_camera_.GetName().c_str());
  }

  if (!active_camera_.HasCamera()) {
    auto camera = std::make_unique<scene::PerspectiveCamera>();
    active_camera_.AttachCamera(std::move(camera));
  }

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
  }
}

void SceneLoaderService::LogSceneHierarchy(const scene::Scene& scene)
{
  LOG_F(INFO, "SceneLoader: Runtime scene hierarchy:");
  std::unordered_set<scene::NodeHandle> visited_nodes;
  visited_nodes.reserve(runtime_nodes_.size());
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

    visited_nodes.insert(node.GetHandle());
    PrintNodeLine(node, depth);

    auto child = node.GetFirstChild();
    while (child) {
      self(self, *child, depth + 1);
      child = child->GetNextSibling();
    }
  };

  for (auto& root : scene.GetRootNodes()) {
    PrintSubtree(PrintSubtree, root, 0);
  }

  if (visited_nodes.size() != runtime_nodes_.size()) {
    LOG_F(WARNING, "SceneLoader: Hierarchy traversal visited {} of {} nodes.",
      visited_nodes.size(), runtime_nodes_.size());
    for (auto& node : runtime_nodes_) {
      if (!node.IsAlive() || visited_nodes.contains(node.GetHandle())) {
        continue;
      }

      const bool has_renderable = node.GetRenderable().HasGeometry();
      const bool has_camera = node.HasCamera();
      const bool has_light = node.HasLight();
      LOG_F(WARNING, "SceneLoader: Unvisited node: {}{}{}{}",
        node.GetName().c_str(), has_renderable ? " [R]" : "",
        has_camera ? " [C]" : "", has_light ? " [L]" : "");
    }
  } else {
    LOG_F(INFO, "SceneLoader: Hierarchy traversal covered all {} nodes.",
      runtime_nodes_.size());
  }
}

} // namespace oxygen::examples
