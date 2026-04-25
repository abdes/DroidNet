//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/SceneSync/RuntimeMotionProducerModule.h>

namespace oxygen::scenesync {

namespace {

  auto AppendNodeHierarchy(const scene::SceneNode& root,
    std::vector<scene::SceneNode>& nodes) -> void
  {
    auto stack = std::vector<scene::SceneNode> {};
    stack.push_back(root);
    while (!stack.empty()) {
      auto node = stack.back();
      stack.pop_back();
      if (!node.IsAlive()) {
        continue;
      }

      nodes.push_back(node);

      auto children = std::vector<scene::SceneNode> {};
      if (auto child = node.GetFirstChild(); child.has_value()) {
        auto current = *child;
        while (current.IsAlive()) {
          children.push_back(current);
          const auto next = current.GetNextSibling();
          if (!next.has_value()) {
            break;
          }
          current = *next;
        }
      }

      for (auto it = children.rbegin(); it != children.rend(); ++it) {
        stack.push_back(*it);
      }
    }
  }

  auto CollectSceneNodes(const scene::Scene& scene) -> std::vector<scene::SceneNode>
  {
    auto nodes = std::vector<scene::SceneNode> {};
    const auto roots = scene.GetRootNodes();
    nodes.reserve(roots.size());
    for (const auto& root : roots) {
      AppendNodeHierarchy(root, nodes);
    }
    return nodes;
  }

  auto BuildMaterialContractHash(const data::MaterialAsset& material)
    -> std::uint64_t
  {
    std::size_t seed = 0U;
    oxygen::HashCombine(seed, material.GetAssetKey());
    oxygen::HashCombine(seed, material.GetMaterialDomain());
    oxygen::HashCombine(seed, material.GetFlags());
    oxygen::HashCombine(seed, material.GetAlphaCutoff());

    const auto uv_scale = material.GetUvScale();
    oxygen::HashCombine(seed, uv_scale[0]);
    oxygen::HashCombine(seed, uv_scale[1]);

    const auto uv_offset = material.GetUvOffset();
    oxygen::HashCombine(seed, uv_offset[0]);
    oxygen::HashCombine(seed, uv_offset[1]);

    oxygen::HashCombine(seed, material.GetUvRotationRadians());
    oxygen::HashCombine(seed, material.GetUvSet());

    for (const auto& shader : material.GetShaders()) {
      oxygen::HashCombine(seed, shader.GetShaderType());
      oxygen::HashCombine(seed, shader.GetSourcePath());
      oxygen::HashCombine(seed, shader.GetEntryPoint());
      oxygen::HashCombine(seed, shader.GetDefines());
      oxygen::HashCombine(seed, shader.GetShaderSourceHash());
    }

    return static_cast<std::uint64_t>(seed);
  }

  auto BuildMaterialMotionState(const scene::NodeHandle node_handle,
    const data::GeometryAsset& geometry, const std::uint32_t lod_index,
    const std::uint32_t submesh_index,
    const std::shared_ptr<const data::MaterialAsset>& material)
    -> PublishedRuntimeMaterialMotionState
  {
    auto state = PublishedRuntimeMaterialMotionState {
      .key = RuntimeMaterialMotionKey {
        .node_handle = node_handle,
        .geometry_asset_key = geometry.GetAssetKey(),
        .lod_index = lod_index,
        .submesh_index = submesh_index,
      },
    };

    if (material) {
      state.resolved_material_asset_key = material->GetAssetKey();
      state.contract_hash = BuildMaterialContractHash(*material);
    }

    return state;
  }

  auto OverlayRuntimeMaterialMotionInput(
    PublishedRuntimeMaterialMotionState& state,
    const RuntimeMaterialMotionInputState& input) -> void
  {
    if (input.resolved_material_asset_key != data::AssetKey {}) {
      state.resolved_material_asset_key = input.resolved_material_asset_key;
    }
    if (input.contract_hash != 0U) {
      state.contract_hash = input.contract_hash;
    }
    state.has_runtime_wpo_input = input.has_runtime_wpo_input;
    state.has_runtime_motion_vector_input
      = input.has_runtime_motion_vector_input;
    state.capabilities = input.capabilities;
    state.wpo_parameter_block0 = input.wpo_parameter_block0;
    state.motion_vector_parameter_block0 = input.motion_vector_parameter_block0;
  }

} // namespace

RuntimeMotionProducerModule::RuntimeMotionProducerModule(
  const engine::ModulePriority priority)
  : priority_(priority)
{
}

auto RuntimeMotionProducerModule::OnSceneMutation(
  const observer_ptr<engine::FrameContext> /*context*/) -> co::Co<>
{
  // Current producer bookkeeping is intentionally minimal in the first parity
  // slice. The authoritative freeze/publication point remains kPublishViews.
  co_return;
}

auto RuntimeMotionProducerModule::OnPublishViews(
  const observer_ptr<engine::FrameContext> context) -> co::Co<>
{
  if (context == nullptr) {
    co_return;
  }

  const auto scene = context->GetScene();
  if (scene == nullptr) {
    co_return;
  }

  PublishSnapshotForScene(*scene, context->GetFrameSequenceNumber());
  co_return;
}

auto RuntimeMotionProducerModule::OnShutdown() noexcept -> void
{
  {
    std::unique_lock lock(published_snapshots_mutex_);
    published_snapshots_.clear();
  }
  {
    std::unique_lock lock(runtime_material_inputs_mutex_);
    runtime_material_inputs_.clear();
  }
}

auto RuntimeMotionProducerModule::GetPublishedSnapshot(
  const observer_ptr<const scene::Scene> scene) const
  -> const PublishedRuntimeMotionSnapshot*
{
  if (scene == nullptr) {
    return nullptr;
  }

  std::shared_lock lock(published_snapshots_mutex_);
  const auto it = published_snapshots_.find(scene.get());
  if (it == published_snapshots_.end()) {
    return nullptr;
  }
  return &it->second;
}

auto RuntimeMotionProducerModule::UpsertMaterialMotionInput(
  const observer_ptr<const scene::Scene> scene,
  const RuntimeMaterialMotionInputState& state) -> void
{
  if (scene == nullptr) {
    return;
  }

  std::unique_lock lock(runtime_material_inputs_mutex_);
  runtime_material_inputs_[scene.get()].insert_or_assign(state.key, state);
}

auto RuntimeMotionProducerModule::RemoveMaterialMotionInput(
  const observer_ptr<const scene::Scene> scene, const RuntimeMaterialMotionKey& key)
  -> void
{
  if (scene == nullptr) {
    return;
  }

  std::unique_lock lock(runtime_material_inputs_mutex_);
  const auto scene_it = runtime_material_inputs_.find(scene.get());
  if (scene_it == runtime_material_inputs_.end()) {
    return;
  }
  scene_it->second.erase(key);
  if (scene_it->second.empty()) {
    runtime_material_inputs_.erase(scene_it);
  }
}

auto RuntimeMotionProducerModule::FindMaterialMotionInput(
  const observer_ptr<const scene::Scene> scene, const RuntimeMaterialMotionKey& key)
  const -> std::optional<RuntimeMaterialMotionInputState>
{
  if (scene == nullptr) {
    return std::nullopt;
  }

  std::shared_lock lock(runtime_material_inputs_mutex_);
  const auto scene_it = runtime_material_inputs_.find(scene.get());
  if (scene_it == runtime_material_inputs_.end()) {
    return std::nullopt;
  }
  const auto input_it = scene_it->second.find(key);
  if (input_it == scene_it->second.end()) {
    return std::nullopt;
  }
  return input_it->second;
}

auto RuntimeMotionProducerModule::PublishSnapshotForScene(
  const scene::Scene& scene, const frame::SequenceNumber frame_sequence) -> void
{
  auto snapshot = PublishedRuntimeMotionSnapshot {
    .frame_sequence = frame_sequence,
  };

  const auto nodes = CollectSceneNodes(scene);
  for (const auto& node : nodes) {
    const auto renderable = node.GetRenderable();
    const auto geometry = renderable.GetGeometry();
    if (!geometry) {
      continue;
    }

    auto published_skinned_state = false;
    const auto lod_count = geometry->LodCount();
    for (std::uint32_t lod_index = 0U;
      lod_index < static_cast<std::uint32_t>(lod_count); ++lod_index) {
      const auto& mesh = geometry->MeshAt(lod_index);
      if (!mesh) {
        continue;
      }

      if (!published_skinned_state && mesh->IsSkinned()) {
        auto state = PublishedSkinnedPoseState {
          .node_handle = node.GetHandle(),
          .geometry_asset_key = geometry->GetAssetKey(),
        };
        // TODO(vortex/skinned-morph): when the engine gains real
        // skeleton/animation runtime ownership, freeze the current joint
        // palette here instead of publishing the current placeholder-only
        // contract hash bridge.
        if (const auto* skinned = mesh->SkinnedDescriptor();
          skinned != nullptr) {
          state.skeleton_asset_key = skinned->skeleton_asset_key;
          std::size_t seed = 0U;
          oxygen::HashCombine(seed, geometry->GetAssetKey());
          oxygen::HashCombine(seed, skinned->skeleton_asset_key);
          oxygen::HashCombine(seed, skinned->joint_count);
          oxygen::HashCombine(seed, skinned->influences_per_vertex);
          oxygen::HashCombine(seed, skinned->flags);
          state.contract_hash = static_cast<std::uint64_t>(seed);
        }
        snapshot.skinned_pose_states.push_back(state);
        published_skinned_state = true;
      }

      const auto submeshes = mesh->SubMeshes();
      for (std::uint32_t submesh_index = 0U;
        submesh_index < static_cast<std::uint32_t>(submeshes.size());
        ++submesh_index) {
        // TODO(vortex/skinned-morph): overlay future authoritative morph /
        // deformation runtime payload here once the engine exposes it. Phase 3
        // closure is scoped to current engine features, so only the existing
        // material-motion families are frozen today.
        const auto material
          = renderable.ResolveSubmeshMaterial(lod_index, submesh_index);
        auto state = BuildMaterialMotionState(
          node.GetHandle(), *geometry, lod_index, submesh_index, material);
        if (const auto input = FindMaterialMotionInput(
              observer_ptr<const scene::Scene> { &scene }, state.key);
          input.has_value()) {
          OverlayRuntimeMaterialMotionInput(state, *input);
        }
        snapshot.material_motion_states.push_back(std::move(state));
      }
    }
  }

  snapshot.material_motion_index.reserve(snapshot.material_motion_states.size());
  for (std::size_t index = 0U; index < snapshot.material_motion_states.size();
    ++index) {
    snapshot.material_motion_index.emplace(
      snapshot.material_motion_states[index].key, index);
  }

  std::unique_lock lock(published_snapshots_mutex_);
  published_snapshots_.insert_or_assign(&scene, std::move(snapshot));
}

} // namespace oxygen::scenesync
