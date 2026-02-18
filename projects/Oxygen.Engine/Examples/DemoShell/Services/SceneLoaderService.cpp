//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"
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
    const glm::vec3& target, const glm::vec3& up_direction = space::move::Up)
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
      up_dir
        = (std::abs(forward.z) > 0.9F) ? space::move::Back : space::move::Up;
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

  auto ScriptParamFromRecord(const data::pak::ScriptParamRecord& record)
    -> std::optional<data::ScriptParam>
  {
    using data::pak::ScriptParamType;
    switch (record.type) {
    case ScriptParamType::kBool:
      return data::ScriptParam { record.value.as_bool };
    case ScriptParamType::kInt32:
      return data::ScriptParam { record.value.as_int32 };
    case ScriptParamType::kFloat:
      return data::ScriptParam { record.value.as_float };
    case ScriptParamType::kString: {
      const auto* const begin = std::begin(record.value.as_string);
      const auto* const end = std::end(record.value.as_string);
      const auto* const nul = std::ranges::find(record.value.as_string, '\0');
      if (nul == end) {
        return std::nullopt;
      }
      return data::ScriptParam { std::string(begin, nul) };
    }
    case ScriptParamType::kVec2:
      return data::ScriptParam { Vec2(
        record.value.as_vec[0], record.value.as_vec[1]) };
    case ScriptParamType::kVec3:
      return data::ScriptParam { Vec3(record.value.as_vec[0],
        record.value.as_vec[1], record.value.as_vec[2]) };
    case ScriptParamType::kVec4:
      return data::ScriptParam { Vec4(record.value.as_vec[0],
        record.value.as_vec[1], record.value.as_vec[2],
        record.value.as_vec[3]) };
    case ScriptParamType::kNone:
    default:
      return std::nullopt;
    }
  }

  constexpr uint64_t kLuauCompilerFingerprint = 0x6C7561755F7631ULL;
  constexpr uint64_t kLuauVmBytecodeVersion = 1ULL;
  constexpr uint64_t kUnknownCompilerFingerprint = 0x756E6B6E6F776E31ULL;
  constexpr uint64_t kUnknownVmBytecodeVersion = 0ULL;
#if defined(_WIN64)
  constexpr uint64_t kPlatformAbiSalt = 0x77696E36345F6D73ULL;
#elif defined(__linux__) && defined(__x86_64__)
  constexpr uint64_t kPlatformAbiSalt = 0x6C6E7836345F6763ULL;
#elif defined(__APPLE__) && defined(__aarch64__)
  constexpr uint64_t kPlatformAbiSalt = 0x6D61635F61726D36ULL;
#else
  constexpr uint64_t kPlatformAbiSalt = 0x756E6B6E6F776E5FULL;
#endif

  auto CompilerFingerprintForLanguage(const data::pak::ScriptLanguage language)
    -> uint64_t
  {
    switch (language) {
    case data::pak::ScriptLanguage::kLuau:
      return kLuauCompilerFingerprint;
    default:
      return kUnknownCompilerFingerprint;
    }
  }

  auto VmBytecodeVersionForLanguage(const data::pak::ScriptLanguage language)
    -> uint64_t
  {
    switch (language) {
    case data::pak::ScriptLanguage::kLuau:
      return kLuauVmBytecodeVersion;
    default:
      return kUnknownVmBytecodeVersion;
    }
  }

  auto ComputeCompileKey(const data::AssetKey asset_key,
    const scripting::ScriptSourceBlob& blob,
    const scripting::CompileMode compile_mode)
    -> scripting::IScriptCompilationService::CompileKey
  {
    const auto bytes = blob.BytesView();
    uint64_t seed = 0;
    oxygen::HashCombine(seed, asset_key);
    oxygen::HashCombine(seed, compile_mode);
    oxygen::HashCombine(seed, blob.Language());
    oxygen::HashCombine(seed, blob.Compression());
    oxygen::HashCombine(seed, blob.GetOrigin());
    oxygen::HashCombine(seed, blob.GetCanonicalName().get());
    oxygen::HashCombine(seed, blob.ContentHash());
    oxygen::HashCombine(seed, CompilerFingerprintForLanguage(blob.Language()));
    oxygen::HashCombine(seed, VmBytecodeVersionForLanguage(blob.Language()));
    oxygen::HashCombine(seed, kPlatformAbiSalt);
    for (const auto byte : bytes) {
      oxygen::HashCombine(seed, byte);
    }
    return scripting::IScriptCompilationService::CompileKey { seed };
  }

  auto HexPreview(std::span<const uint8_t> bytes, const size_t max_bytes)
    -> std::string
  {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto count = std::min(bytes.size(), max_bytes);
    if (count == 0) {
      return "<empty>";
    }

    std::string out;
    out.reserve(count * 3);
    for (size_t i = 0; i < count; ++i) {
      const auto value = bytes[i];
      out.push_back(kHex[(value >> 4U) & 0x0FU]);
      out.push_back(kHex[value & 0x0FU]);
      if (i + 1 < count) {
        out.push_back(' ');
      }
    }
    return out;
  }

} // namespace

SceneLoaderService::SceneLoaderService(content::IAssetLoader& loader,
  const Extent<uint32_t> viewport, std::filesystem::path source_pak_path,
  observer_ptr<scripting::IScriptCompilationService> compilation_service,
  PathFinder path_finder)
  : loader_(loader)
  , extent_(viewport)
  , compilation_service_(std::move(compilation_service))
  , source_resolver_(
      std::make_unique<scripting::ScriptSourceResolver>(std::move(path_finder)))
{
  if (!source_pak_path.empty()) {
    source_pak_ = std::make_unique<content::PakFile>(source_pak_path);
  }
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
    // Scene dependencies are published by AssetLoader during scene decode.
    // Do not acquire/release additional geometry "pins" here, because using
    // ReleaseAsset() for temporary pins would recursively release dependency
    // edges and can unpin live texture resources.
    ready_ = true;
    failed_ = false;
    pending_geometry_keys_.clear();
    pinned_geometry_keys_.clear();
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
 loader references alive until `BuildSceneAsync()` completes. It prevents
 rapid
 swaps from evicting geometry between dependency resolution and attachment.

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
  (void)asset;
  ready_ = true;
  pending_geometry_keys_.clear();
  pinned_geometry_keys_.clear();
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
  // Intentionally non-destructive: geometry dependency ownership is tracked by
  // AssetLoader's scene/material dependency graph, and explicit ReleaseAsset()
  // here can tear down live dependency edges.
  pinned_geometry_keys_.clear();
  pending_geometry_keys_.clear();
}

auto SceneLoaderService::BuildSceneAsync(scene::Scene& scene,
  const data::SceneAsset& asset) -> co::Co<scene::SceneNode>
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
  AttachScripting(asset);
  SelectActiveCamera(asset);
  EnsureCameraAndViewport(scene);
  // Geometry pins are only needed until scene instantiation finishes.
  ReleasePinnedGeometryAssets();
  LogSceneHierarchy(scene);

  LOG_F(INFO, "SceneLoader: Runtime scene instantiation complete.");
  co_return std::move(active_camera_);
}

auto SceneLoaderService::BuildEnvironment(const data::SceneAsset& asset)
  -> std::unique_ptr<scene::SceneEnvironment>
{
  auto environment = std::make_unique<scene::SceneEnvironment>();
  EnvironmentSettingsService::HydrateEnvironment(*environment, asset);
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
    // intensity REMOVED from common - set via specific light class methods
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
    light->SetIntensityLux(rec.intensity_lux);
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
    light->SetLuminousFluxLm(rec.luminous_flux_lm);
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
    light->SetLuminousFluxLm(rec.luminous_flux_lm);
    light->SetRange(std::abs(rec.range));
    light->SetAttenuationModel(
      static_cast<scene::AttenuationModel>(rec.attenuation_model));
    light->SetDecayExponent(rec.decay_exponent);
    light->SetInnerConeAngleRadians(rec.inner_cone_angle_radians);
    light->SetOuterConeAngleRadians(rec.outer_cone_angle_radians);
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

void SceneLoaderService::AttachScripting(const data::SceneAsset& asset)
{
  using data::pak::ScriptingComponentRecord;

  const auto scripting_components
    = asset.GetComponents<ScriptingComponentRecord>();
  if (scripting_components.empty()) {
    return;
  }
  LOG_F(INFO, "hydrating scripting components (count={})",
    scripting_components.size());

  for (const ScriptingComponentRecord& component : scripting_components) {
    const auto node_index = static_cast<size_t>(component.node_index);
    if (node_index >= runtime_nodes_.size()) {
      LOG_F(WARNING, "invalid scripting component node index {}", node_index);
      continue;
    }

    if (!runtime_nodes_[node_index].HasScripting()) {
      const bool attached = runtime_nodes_[node_index].AttachScripting();
      if (!attached && !runtime_nodes_[node_index].HasScripting()) {
        LOG_F(
          ERROR, "failed to attach scripting component to node {}", node_index);
        continue;
      }
    }
    auto scripting = runtime_nodes_[node_index].GetScripting();
    const auto slot_records = loader_.GetHydratedScriptSlots(asset, component);
    LOG_F(INFO, "hydrated script slots (node_index={}, slots={})", node_index,
      slot_records.size());

    for (const auto& slot_record : slot_records) {
      if (slot_record.script_asset_key == data::AssetKey {}) {
        continue;
      }

      auto script_asset = loader_.GetScriptAsset(slot_record.script_asset_key);
      if (!script_asset) {
        LOG_F(ERROR, "missing script asset {}",
          data::to_string(slot_record.script_asset_key));
        continue;
      }

      if (!scripting.AddSlot(script_asset)) {
        LOG_F(ERROR, "failed to attach script slot to node {}", node_index);
        continue;
      }
      LOG_F(INFO, "attached script slot (node_index={}, script_asset={})",
        node_index, data::to_string(slot_record.script_asset_key));

      const auto slots = scripting.Slots();
      if (slots.empty()) {
        continue;
      }
      const auto slot = slots.back();

      if (!slot_record.params.empty()) {
        ApplySlotParameters(scripting, slot, slot_record.params);
        LOG_F(INFO, "applied slot parameters (node_index={}, count={})",
          node_index, slot_record.params.size());
      }

      QueueSlotCompilation(
        runtime_nodes_[node_index], slot, std::move(script_asset));
    }
  }
}

void SceneLoaderService::ApplySlotParameters(
  scene::SceneNode::Scripting& scripting,
  const scene::SceneNode::Scripting::Slot& slot,
  std::span<const data::pak::ScriptParamRecord> params)
{
  for (const auto& param : params) {
    const auto* const key_begin = std::begin(param.key);
    const auto key_end = std::ranges::find(param.key, '\0');
    if (key_end == std::end(param.key)) {
      LOG_F(WARNING, "script parameter key is not null-terminated");
      continue;
    }
    const std::string_view key(
      key_begin, static_cast<size_t>(key_end - key_begin));
    const auto value = ScriptParamFromRecord(param);
    if (!value.has_value()) {
      LOG_F(WARNING, "skipping unsupported script parameter type");
      continue;
    }
    if (!scripting.SetParameter(slot, key, *value)) {
      LOG_F(WARNING, "failed to set script parameter '{}'", key);
    }
  }
}

void SceneLoaderService::QueueSlotCompilation(scene::SceneNode node,
  const scene::SceneNode::Scripting::Slot& slot,
  std::shared_ptr<const data::ScriptAsset> script_asset)
{
  if (!script_asset) {
    LOG_F(ERROR, "script compilation skipped because script asset is null");
    return;
  }
  if (!compilation_service_) {
    LOG_F(ERROR,
      "script compilation skipped because compilation service is unavailable");
    return;
  }

  auto load_script_resource
    = [pak = source_pak_.get()](
        const uint32_t index) -> std::shared_ptr<const data::ScriptResource> {
    if (pak == nullptr) {
      LOG_F(WARNING,
        "script resource load requested without source pak (index={})", index);
      return nullptr;
    }
    try {
      return pak->ReadScriptResource(index);
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "failed to read script resource (index={}): {}", index,
        ex.what());
      return nullptr;
    } catch (...) {
      LOG_F(WARNING, "failed to read script resource (index={})", index);
      return nullptr;
    }
  };

  auto map_origin
    = [pak = source_pak_.get()](
        const uint32_t index) -> std::optional<scripting::ScriptBlobOrigin> {
    if (pak == nullptr) {
      LOG_F(WARNING,
        "script origin mapping requested without source pak (index={})", index);
      return std::nullopt;
    }
    try {
      const auto resource = pak->ReadScriptResource(index);
      if (!resource) {
        LOG_F(WARNING,
          "script resource missing while mapping origin (index={})", index);
        return std::nullopt;
      }
      return scripting::ScriptBlobOrigin::kEmbeddedResource;
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "failed to map script origin from resource (index={}): {}",
        index, ex.what());
      return std::nullopt;
    } catch (...) {
      LOG_F(
        WARNING, "failed to map script origin from resource (index={})", index);
      return std::nullopt;
    }
  };

  auto resolve_result = source_resolver_->Resolve(
    scripting::IScriptSourceResolver::ResolveRequest {
      .asset = *script_asset,
      .load_script_resource = std::move(load_script_resource),
      .map_resource_origin = std::move(map_origin),
    });
  if (!resolve_result.ok) {
    LOG_F(ERROR, "failed to resolve script source: {}",
      resolve_result.error_message);
    return;
  }
  if (!resolve_result.blob.has_value()) {
    LOG_F(ERROR, "resolved script blob is missing (asset_key={})",
      data::to_string(script_asset->GetAssetKey()));
    return;
  }
  auto resolved_blob = std::move(*resolve_result.blob);
  const auto is_bytecode
    = std::holds_alternative<scripting::ScriptBytecodeBlob>(resolved_blob);
  const auto origin = std::visit(
    [](const auto& blob) { return static_cast<uint32_t>(blob.GetOrigin()); },
    resolved_blob);
  const auto size
    = std::visit([](const auto& blob) { return blob.Size(); }, resolved_blob);
  const auto bytes = std::visit(
    [](const auto& blob) { return blob.BytesView(); }, resolved_blob);
  LOG_F(INFO,
    "resolved script source (asset_key={}, origin={}, bytecode={}, size={})",
    data::to_string(script_asset->GetAssetKey()), origin,
    is_bytecode ? "yes" : "no", size);
  LOG_F(INFO, "resolved script source preview (asset_key={}, first_bytes={})",
    data::to_string(script_asset->GetAssetKey()), HexPreview(bytes, 16));
  if (size == 0) {
    LOG_F(ERROR, "resolved script source is empty (asset_key={})",
      data::to_string(script_asset->GetAssetKey()));
    auto scripting = node.GetScripting();
    (void)scripting.MarkSlotCompilationFailed(
      slot, "resolved script source is empty");
    return;
  }

  if (is_bytecode) {
    auto bytecode_blob
      = std::get<scripting::ScriptBytecodeBlob>(std::move(resolved_blob));
    auto executable_blob
      = std::make_shared<const scripting::ScriptBytecodeBlob>(
        std::move(bytecode_blob));
    auto scripting = node.GetScripting();
    (void)scripting.MarkSlotReady(slot,
      std::make_shared<const scripting::CompiledScriptExecutable>(
        std::move(executable_blob)));
    LOG_F(INFO, "slot ready from embedded bytecode (asset_key={})",
      data::to_string(script_asset->GetAssetKey()));
    return;
  }

  auto source_blob
    = std::get<scripting::ScriptSourceBlob>(std::move(resolved_blob));
  constexpr auto kCompileMode = scripting::CompileMode::kDebug;
  const auto compile_key
    = ComputeCompileKey(script_asset->GetAssetKey(), source_blob, kCompileMode);
  const auto source_size = source_blob.Size();
  LOG_F(INFO,
    "submitting compile request (asset_key={}, compile_key={}, source_size={})",
    data::to_string(script_asset->GetAssetKey()), compile_key, source_size);
  auto acquire = compilation_service_->AcquireForSlot(
    scripting::IScriptCompilationService::Request {
      .compile_key = compile_key,
      .source = std::move(source_blob),
      .compile_mode = kCompileMode,
    },
    scripting::IScriptCompilationService::SlotAcquireCallbacks {
      .on_ready =
        [node, slot](std::shared_ptr<const scripting::ScriptBytecodeBlob>
            bytecode) mutable {
          if (node.IsAlive()) {
            auto scripting = node.GetScripting();
            (void)scripting.MarkSlotReady(slot,
              std::make_shared<const scripting::CompiledScriptExecutable>(
                std::move(bytecode)));
            LOG_F(INFO, "slot ready from compilation");
          }
        },
      .on_failed =
        [node, slot](std::string diagnostic) mutable {
          if (node.IsAlive()) {
            auto scripting = node.GetScripting();
            LOG_F(ERROR, "script compilation failed: {}", diagnostic);
            (void)scripting.MarkSlotCompilationFailed(
              slot, std::move(diagnostic));
          }
        },
    });
  (void)acquire;
  LOG_F(INFO, "queued script compilation (asset_key={}, compile_key={})",
    data::to_string(script_asset->GetAssetKey()), compile_key);
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
        const glm::vec3 forward = cam_rot * space::look::Forward;
        const glm::vec3 up = cam_rot * space::look::Up;
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
  const float aspect = extent_.height > 0
    ? (static_cast<float>(extent_.width) / static_cast<float>(extent_.height))
    : 1.0F;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent_.width),
    .height = static_cast<float>(extent_.height),
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
