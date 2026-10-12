//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "DemoShell/Services/SceneLoaderService.h"
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Content/ContentLoadScope.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Shape.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/SceneInstantiation/SceneInstantiation.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>
#include <Oxygen/Scripting/Resolver/ScriptSourceResolver.h>

namespace oxygen::examples {

namespace {

  constexpr std::string_view kSceneName = "RenderScene";

  //! Squared length below which a direction is degenerate.
  constexpr float kDegenerateLengthSquared = 1e-8F;
  //! |cos| above which forward is treated as colinear with up.
  constexpr float kColinearCosine = 0.999F;
  //! |forward.z| above which Back is the alternate up direction.
  constexpr float kVerticalForwardZ = 0.9F;
  //! Tolerance of IsNearlyEqual.
  constexpr float kNearlyEqualTolerance = 1e-5F;
  //! Smallest dynamic body mass derived from a material density, in kg.
  constexpr float kMinDynamicMassKg = 0.001F;

  auto MakeLookRotationFromPosition(
    const glm::vec3& position, const glm::vec3& target) -> glm::quat
  {
    const auto forward_raw = target - position;
    const float forward_len2 = glm::dot(forward_raw, forward_raw);
    if (forward_len2 <= kDegenerateLengthSquared) {
      return { 1.0F, 0.0F, 0.0F, 0.0F };
    }

    const auto forward = glm::normalize(forward_raw);
    // Avoid singularities when forward is colinear with up.
    glm::vec3 up_dir = space::move::Up;
    const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
    if (dot_abs > kColinearCosine) {
      // Pick an alternate up that is guaranteed to be non-colinear.
      up_dir = (std::abs(forward.z) > kVerticalForwardZ) ? space::move::Back
                                                         : space::move::Up;
    }

    const auto right_raw = glm::cross(forward, up_dir);
    const float right_len2 = glm::dot(right_raw, right_raw);
    if (right_len2 <= kDegenerateLengthSquared) {
      return { 1.0F, 0.0F, 0.0F, 0.0F };
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

  auto IsNearlyEqual(const float lhs, const float rhs) noexcept -> bool
  {
    return std::abs(lhs - rhs) <= kNearlyEqualTolerance;
  }

  auto IsUniformScale(const Vec3& scale) noexcept -> bool
  {
    return IsNearlyEqual(scale.x, scale.y) && IsNearlyEqual(scale.y, scale.z);
  }

  auto IsValidFiniteScale(const Vec3& scale) noexcept -> bool
  {
    return std::isfinite(scale.x) && std::isfinite(scale.y)
      && std::isfinite(scale.z) && scale.x > 0.0F && scale.y > 0.0F
      && scale.z > 0.0F;
  }

  auto IsFiniteVec3(const Vec3& value) noexcept -> bool
  {
    return std::isfinite(value.x) && std::isfinite(value.y)
      && std::isfinite(value.z);
  }

  auto ResolveActiveBackend(const observer_ptr<AsyncEngine> engine,
    const std::string_view operation) -> EnginePhysicsBackend
  {
    if (!engine) {
      throw std::runtime_error(
        std::string(operation) + " requires a live engine instance");
    }
    return engine->GetEngineConfig().physics.backend;
  }

  auto ExpectedJointResourceFormat(const EnginePhysicsBackend backend)
    -> std::optional<data::pak::physics::PhysicsResourceFormat>
  {
    switch (backend) {
    case EnginePhysicsBackend::kJolt:
      return data::pak::physics::PhysicsResourceFormat::kJoltConstraintBinary;
    case EnginePhysicsBackend::kPhysX:
      return data::pak::physics::PhysicsResourceFormat::kPhysXConstraintBinary;
    default:
      return std::nullopt;
    }
  }

  auto ExpectedVehicleResourceFormat(const EnginePhysicsBackend backend)
    -> std::optional<data::pak::physics::PhysicsResourceFormat>
  {
    switch (backend) {
    case EnginePhysicsBackend::kJolt:
      return data::pak::physics::PhysicsResourceFormat::
        kJoltVehicleConstraintBinary;
    case EnginePhysicsBackend::kPhysX:
      return data::pak::physics::PhysicsResourceFormat::
        kPhysXVehicleSettingsBinary;
    default:
      return std::nullopt;
    }
  }

  auto ExpectedShapeResourceFormat(const EnginePhysicsBackend backend,
    const data::pak::physics::ShapePayloadType payload_type)
    -> std::optional<data::pak::physics::PhysicsResourceFormat>
  {
    switch (backend) {
    case EnginePhysicsBackend::kJolt:
      return data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary;
    case EnginePhysicsBackend::kPhysX:
      switch (payload_type) {
      case data::pak::physics::ShapePayloadType::kConvex:
        return data::pak::physics::PhysicsResourceFormat::
          kPhysXConvexMeshBinary;
      case data::pak::physics::ShapePayloadType::kMesh:
        return data::pak::physics::PhysicsResourceFormat::
          kPhysXTriangleMeshBinary;
      case data::pak::physics::ShapePayloadType::kHeightField:
        return data::pak::physics::PhysicsResourceFormat::
          kPhysXHeightFieldBinary;
      case data::pak::physics::ShapePayloadType::kCompound:
      case data::pak::physics::ShapePayloadType::kInvalid:
      default:
        return std::nullopt;
      }
    default:
      return std::nullopt;
    }
  }

  auto ToRuntimeSoftBodyTetherMode(
    const data::pak::physics::SoftBodyTetherMode mode)
    -> physics::softbody::SoftBodyTetherMode
  {
    switch (mode) {
    case data::pak::physics::SoftBodyTetherMode::kNone:
      return physics::softbody::SoftBodyTetherMode::kNone;
    case data::pak::physics::SoftBodyTetherMode::kEuclidean:
      return physics::softbody::SoftBodyTetherMode::kEuclidean;
    case data::pak::physics::SoftBodyTetherMode::kGeodesic:
      return physics::softbody::SoftBodyTetherMode::kGeodesic;
    default:
      return physics::softbody::SoftBodyTetherMode::kNone;
    }
  }

  auto ToRuntimeAggregateAuthority(
    const data::pak::physics::AggregateAuthority authority)
    -> physics::aggregate::AggregateAuthority
  {
    switch (authority) {
    case data::pak::physics::AggregateAuthority::kSimulation:
      return physics::aggregate::AggregateAuthority::kSimulation;
    case data::pak::physics::AggregateAuthority::kCommand:
      return physics::aggregate::AggregateAuthority::kCommand;
    default:
      return physics::aggregate::AggregateAuthority::kSimulation;
    }
  }

  auto ToRuntimeVehicleWheelSide(
    const data::pak::physics::VehicleWheelSide side)
    -> physics::vehicle::VehicleWheelSide
  {
    switch (side) {
    case data::pak::physics::VehicleWheelSide::kLeft:
      return physics::vehicle::VehicleWheelSide::kLeft;
    case data::pak::physics::VehicleWheelSide::kRight:
      return physics::vehicle::VehicleWheelSide::kRight;
    default:
      return physics::vehicle::VehicleWheelSide::kLeft;
    }
  }

  auto ToRuntimeVehicleControllerType(
    const data::pak::physics::VehicleControllerType controller_type)
    -> physics::vehicle::VehicleControllerType
  {
    switch (controller_type) {
    case data::pak::physics::VehicleControllerType::kTracked:
      return physics::vehicle::VehicleControllerType::kTracked;
    case data::pak::physics::VehicleControllerType::kWheeled:
    default:
      return physics::vehicle::VehicleControllerType::kWheeled;
    }
  }

  auto IsDescendantNode(const std::vector<scene::SceneNode>& runtime_nodes,
    const size_t candidate_index, const size_t ancestor_index) -> bool
  {
    if (candidate_index >= runtime_nodes.size()
      || ancestor_index >= runtime_nodes.size()
      || candidate_index == ancestor_index) {
      return false;
    }

    auto current = runtime_nodes.at(candidate_index);
    const auto ancestor_handle = runtime_nodes.at(ancestor_index).GetHandle();
    while (true) {
      const auto parent_opt = current.GetParent();
      if (!parent_opt.has_value()) {
        return false;
      }
      if (parent_opt->GetHandle() == ancestor_handle) {
        return true;
      }
      current = *parent_opt;
    }
  }

  auto CollectVehicleAssociatedRigidBodyNodeIndices(
    const std::span<const data::pak::physics::VehicleBindingRecord>
      vehicle_bindings,
    const std::span<const data::pak::physics::VehicleWheelBindingRecord>
      vehicle_wheel_bindings) -> std::unordered_set<uint32_t>
  {
    auto forbidden_node_indices = std::unordered_set<uint32_t> {};
    forbidden_node_indices.reserve(
      vehicle_bindings.size() + vehicle_wheel_bindings.size());

    for (const auto& vehicle : vehicle_bindings) {
      forbidden_node_indices.insert(vehicle.node_index);
      for (const auto& wheel : vehicle_wheel_bindings) {
        if (wheel.vehicle_node_index == vehicle.node_index) {
          forbidden_node_indices.insert(wheel.wheel_node_index);
        }
      }
    }

    return forbidden_node_indices;
  }

  auto CollectSubtreeRigidBodyNodeIndices(
    const std::span<const data::pak::physics::RigidBodyBindingRecord>
      rigid_body_bindings,
    const std::vector<scene::SceneNode>& runtime_nodes,
    const uint32_t root_node_index) -> std::vector<uint32_t>
  {
    auto member_node_indices = std::vector<uint32_t> {};
    const auto root_index = static_cast<size_t>(root_node_index);
    for (const auto& rigid_body : rigid_body_bindings) {
      const auto candidate_index = static_cast<size_t>(rigid_body.node_index);
      if (candidate_index == root_index) {
        member_node_indices.push_back(rigid_body.node_index);
        continue;
      }
      if (!IsDescendantNode(runtime_nodes, candidate_index, root_index)) {
        continue;
      }
      member_node_indices.push_back(rigid_body.node_index);
    }

    std::ranges::sort(member_node_indices);
    member_node_indices.erase(std::ranges::unique(member_node_indices).begin(),
      member_node_indices.end());
    return member_node_indices;
  }

} // namespace

SceneLoaderService::SceneLoaderService(content::IAssetLoader& loader,
  const Extent<uint32_t> viewport, std::filesystem::path source_pak_path,
  const observer_ptr<AsyncEngine> engine,
  const observer_ptr<engine::InputSystem> input_system,
  observer_ptr<scripting::IScriptCompilationService> compilation_service,
  PathFinder path_finder)
  : loader_(loader)
  , extent_(viewport)
  , source_pak_path_(std::move(source_pak_path))
  , path_finder_(std::move(path_finder))
  , engine_(engine)
  , input_system_(input_system)
  , compilation_service_(compilation_service)
  , source_resolver_(
      std::make_unique<scripting::ScriptSourceResolver>(path_finder_))
{
  if (engine_) {
    physics_module_subscription_ = engine_->SubscribeModuleAttached(
      [this](
        const engine::ModuleEvent& event) -> void { OnModuleAttached(event); },
      /*replay_existing=*/true);
  }
}

SceneLoaderService::~SceneLoaderService()
{
  physics_module_subscription_.Cancel();
  // Ensure any geometry pins are released if the loader is torn down early.
  ClearGeometryReadiness();
  LOG_F(INFO, "SceneLoader: Destroying loader.");
}

void SceneLoaderService::StartLoad(const data::AssetKey& key)
{
  LOG_F(INFO, "SceneLoader: Starting load for scene key: {}",
    oxygen::data::to_string(key));

  if (!source_pak_path_.empty()) {
    // Refresh the source resolver to ensure we pick up any changes on disk
    // since the last load.
    LOG_F(INFO, "SceneLoader: Refreshing script sources from: {}",
      source_pak_path_.string());

    // Recreate the resolver to clear any internal caches.
    source_resolver_
      = std::make_unique<scripting::ScriptSourceResolver>(path_finder_);
  }

  swap_.scene_key = key;
  swap_.asset.reset();
  swap_.physics_asset.reset();
  runtime_scene_ = observer_ptr<scene::Scene> { nullptr };
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;
  ready_ = false;
  failed_ = false;
  consumed_ = false;
  // Start loading the scene asset
  const auto scope = loader_.BeginLoadScope();
  loader_.StartLoadScene(key, content::LoadRequest { .scope = scope },
    [weak_self = weak_from_this(), scope](
      std::shared_ptr<data::SceneAsset> asset) -> void {
      if (auto self = weak_self.lock()) {
        self->OnSceneLoaded(std::move(asset), scope);
      }
    });
}

void SceneLoaderService::MarkConsumed()
{
  consumed_ = true;
  swap_.asset.reset();
  swap_.physics_asset.reset();
  runtime_scene_ = observer_ptr<scene::Scene> { nullptr };
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;
  runtime_nodes_.clear();
  active_camera_ = {};
  // Drop any pins that were never released due to early consumption.
  ClearGeometryReadiness();
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

void SceneLoaderService::OnSceneLoaded(
  std::shared_ptr<data::SceneAsset> asset, content::ContentLoadScope scope)
{
  try {
    if (!asset) {
      LOG_F(ERROR, "SceneLoader: Failed to load scene asset");
      failed_ = true;
      return;
    }

    LOG_F(INFO, "SceneLoader: Scene asset loaded. Validating physics sidecar.");

    runtime_nodes_.clear();
    active_camera_ = {};
    pending_geometry_keys_.clear();
    ready_geometry_keys_.clear();

    const auto scene_asset = std::move(asset);
    const auto sidecar_key_opt
      = loader_.FindPhysicsSidecarAssetKeyForScene(*scene_asset, scope);

    if (!sidecar_key_opt.has_value()) {
      LOG_F(INFO,
        "SceneLoader: No physics sidecar found for scene key={} (scene-only "
        "load).",
        data::to_string(scene_asset->GetAssetKey()));
      swap_.asset = scene_asset;
      swap_.physics_asset.reset();
      ready_ = true;
      failed_ = false;
      return;
    }

    const auto sidecar_key = *sidecar_key_opt;
    loader_.StartLoadPhysicsSceneAsset(sidecar_key,
      content::LoadRequest { .scope = std::move(scope) },
      [weak_self = weak_from_this(), scene_asset, sidecar_key](
        std::shared_ptr<data::PhysicsSceneAsset> physics_asset) -> void {
        if (const auto self = weak_self.lock()) {
          self->OnPhysicsSceneLoaded(
            scene_asset, sidecar_key, std::move(physics_asset));
        }
      });
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

void SceneLoaderService::OnPhysicsSceneLoaded(
  std::shared_ptr<data::SceneAsset> scene_asset, data::AssetKey sidecar_key,
  std::shared_ptr<data::PhysicsSceneAsset> physics_asset)
{
  try {
    if (!scene_asset) {
      throw std::runtime_error(
        "physics-sidecar callback received null scene asset");
    }
    if (!physics_asset) {
      throw std::runtime_error(
        std::string("missing physics sidecar asset for scene key=")
        + data::to_string(scene_asset->GetAssetKey())
        + " sidecar_key=" + data::to_string(sidecar_key));
    }

    ValidatePhysicsSidecarIdentity(*scene_asset, *physics_asset, sidecar_key);

    swap_.asset = std::move(scene_asset);
    swap_.physics_asset = std::move(physics_asset);
    ready_ = true;
    failed_ = false;
    LOG_F(INFO,
      "SceneLoader: Physics sidecar validated for scene key={} sidecar_key={}",
      data::to_string(swap_.scene_key), data::to_string(sidecar_key));
  } catch (const std::exception& ex) {
    LOG_F(
      ERROR, "SceneLoader: Physics sidecar validation failed: {}", ex.what());
    swap_ = {};
    runtime_nodes_.clear();
    active_camera_ = {};
    ready_ = false;
    failed_ = true;
  } catch (...) {
    LOG_F(ERROR, "SceneLoader: Unknown physics sidecar validation failure");
    swap_ = {};
    runtime_nodes_.clear();
    active_camera_ = {};
    ready_ = false;
    failed_ = true;
  }
}

void SceneLoaderService::ValidatePhysicsSidecarIdentity(
  const data::SceneAsset& scene_asset,
  const data::PhysicsSceneAsset& physics_asset,
  const data::AssetKey& sidecar_key) const
{
  const auto& target_scene_key = physics_asset.GetTargetSceneKey();
  if (target_scene_key != scene_asset.GetAssetKey()) {
    throw std::runtime_error(
      std::string("physics sidecar target_scene_key mismatch sidecar_key=")
      + data::to_string(sidecar_key)
      + " scene_key=" + data::to_string(scene_asset.GetAssetKey())
      + " target_scene_key=" + data::to_string(target_scene_key));
  }

  const auto scene_node_count = scene_asset.GetNodes().size();
  if (scene_node_count
    > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error("scene node count exceeds uint32 range");
  }
  const auto scene_node_count_u32 = static_cast<uint32_t>(scene_node_count);
  const auto target_node_count = physics_asset.GetTargetNodeCount();
  if (target_node_count != scene_node_count_u32) {
    throw std::runtime_error(
      std::string("physics sidecar target_node_count mismatch sidecar_key=")
      + data::to_string(sidecar_key)
      + " expected=" + std::to_string(scene_node_count_u32)
      + " actual=" + std::to_string(target_node_count));
  }

  const auto scene_digest = base::ComputeSha256(scene_asset.GetRawData());
  const auto target_scene_content_hash
    = physics_asset.GetTargetSceneContentHash();
  if (!std::ranges::equal(target_scene_content_hash, scene_digest)) {
    throw std::runtime_error(
      std::string(
        "physics sidecar target_scene_content_hash mismatch sidecar_key=")
      + data::to_string(sidecar_key)
      + " scene_key=" + data::to_string(scene_asset.GetAssetKey()));
  }
}

void SceneLoaderService::OnModuleAttached(const engine::ModuleEvent& event)
{
  if (event.type_id != physics::PhysicsModule::ClassTypeId()) {
    return;
  }
  physics_module_ = observer_ptr<physics::PhysicsModule> {
    // NOLINTNEXTLINE(*-static-cast-downcast)
    static_cast<physics::PhysicsModule*>(event.module.get()),
  };
}

auto SceneLoaderService::ResolvePhysicsModule()
  -> observer_ptr<physics::PhysicsModule>
{
  if (!engine_) {
    return nullptr;
  }
  if (auto module_ref = engine_->GetModule<physics::PhysicsModule>()) {
    physics_module_
      = observer_ptr<physics::PhysicsModule> { &module_ref->get() };
    return physics_module_;
  }
  physics_module_ = nullptr;
  return nullptr;
}

auto SceneLoaderService::ResolveCollisionShapeAsset(
  const data::AssetKey& shape_asset_key, const std::string_view binding_kind,
  const uint32_t node_index) -> data::pak::physics::CollisionShapeAssetDesc
{
  if (shape_asset_key.IsNil()) {
    throw std::runtime_error(std::string(binding_kind)
      + " shape_asset_key is nil for node_index=" + std::to_string(node_index));
  }
  if (!current_physics_context_) {
    throw std::runtime_error(std::string(binding_kind)
      + " shape_asset_key resolution requires active physics hydration "
        "context (node_index="
      + std::to_string(node_index)
      + " shape_asset_key=" + data::to_string(shape_asset_key) + ")");
  }
  const auto shape_desc_opt = loader_.ReadCollisionShapeAssetDescForAsset(
    *current_physics_context_, shape_asset_key);
  if (!shape_desc_opt.has_value()) {
    throw std::runtime_error(std::string(binding_kind)
      + " shape_asset_key could not be resolved from content source "
        "(node_index="
      + std::to_string(node_index)
      + " shape_asset_key=" + data::to_string(shape_asset_key) + ")");
  }
  return *shape_desc_opt;
}

auto SceneLoaderService::ResolvePhysicsMaterialAsset(
  const data::AssetKey& material_asset_key, const std::string_view binding_kind,
  const uint32_t node_index) -> data::pak::physics::PhysicsMaterialAssetDesc
{
  if (material_asset_key.IsNil()) {
    throw std::runtime_error(std::string(binding_kind)
      + " material_asset_key is nil for node_index="
      + std::to_string(node_index));
  }
  if (!current_physics_context_) {
    throw std::runtime_error(std::string(binding_kind)
      + " material_asset_key resolution requires active physics hydration "
        "context (node_index="
      + std::to_string(node_index)
      + " material_asset_key=" + data::to_string(material_asset_key) + ")");
  }
  const auto material_desc_opt = loader_.ReadPhysicsMaterialAssetDescForAsset(
    *current_physics_context_, material_asset_key);
  if (!material_desc_opt.has_value()) {
    throw std::runtime_error(std::string(binding_kind)
      + " material_asset_key could not be resolved from content source "
        "(node_index="
      + std::to_string(node_index)
      + " material_asset_key=" + data::to_string(material_asset_key) + ")");
  }
  return *material_desc_opt;
}

auto SceneLoaderService::BuildCollisionShapeFromDescriptor(
  const data::pak::physics::CollisionShapeAssetDesc& shape_desc,
  const std::string_view binding_kind, const uint32_t node_index)
  -> physics::CollisionShape
{
  const auto ensure_no_cooked_ref = [&] -> void {
    if (!shape_desc.cooked_shape_ref.payload_asset_key.IsNil()
      || shape_desc.cooked_shape_ref.payload_type
        != data::pak::physics::kInvalidShapePayloadType) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-005: cooked_shape_ref must be invalid for "
                    "descriptor-only shape in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
  };
  if (shape_desc.shape_type == data::pak::physics::ShapeType::kInvalid) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-001: invalid shape_type in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + ")");
  }
  if (shape_desc.collision_own_layer == 0ULL
    || ((shape_desc.collision_own_layer
          & (shape_desc.collision_own_layer - 1ULL))
      != 0ULL)) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-002: collision_own_layer must be single-bit in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + ")");
  }
  if (shape_desc.is_sensor != 0U && shape_desc.is_sensor != 1U) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-003: is_sensor must be 0 or 1 in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + ")");
  }
  const Vec3 local_scale(shape_desc.local_scale[0], shape_desc.local_scale[1],
    shape_desc.local_scale[2]);
  const bool ignore_local_scale
    = shape_desc.shape_type == data::pak::physics::ShapeType::kPlane
    || shape_desc.shape_type == data::pak::physics::ShapeType::kWorldBoundary;
  if (!ignore_local_scale && !IsValidFiniteScale(local_scale)) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-008: invalid local_scale in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + ")");
  }
  if (!ignore_local_scale && !IsUniformScale(local_scale)
    && (shape_desc.shape_type == data::pak::physics::ShapeType::kSphere
      || shape_desc.shape_type == data::pak::physics::ShapeType::kCapsule
      || shape_desc.shape_type == data::pak::physics::ShapeType::kCylinder
      || shape_desc.shape_type == data::pak::physics::ShapeType::kCone)) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-008: non-uniform local_scale "
                  "is not valid for shape_type in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + " shape_type="
      + std::to_string(static_cast<uint32_t>(shape_desc.shape_type)) + ")");
  }

  switch (shape_desc.shape_type) {
  case data::pak::physics::ShapeType::kSphere: {
    const float radius = shape_desc.shape_params.sphere.radius;
    ensure_no_cooked_ref();
    if (!(radius > 0.0F)) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid sphere radius in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::SphereShape { .radius = radius };
  }
  case data::pak::physics::ShapeType::kCapsule: {
    const float radius = shape_desc.shape_params.capsule.radius;
    const float half_height = shape_desc.shape_params.capsule.half_height;
    ensure_no_cooked_ref();
    if (!std::isfinite(radius) || !std::isfinite(half_height) || radius <= 0.0F
      || half_height < 0.0F) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid capsule params in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::CapsuleShape {
      .radius = radius,
      .half_height = half_height,
    };
  }
  case data::pak::physics::ShapeType::kBox: {
    const auto& half_extents = shape_desc.shape_params.box.half_extents;
    ensure_no_cooked_ref();
    if (!(half_extents[0] > 0.0F) || !(half_extents[1] > 0.0F)
      || !(half_extents[2] > 0.0F)) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid box half_extents in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::BoxShape {
      .extents = Vec3(half_extents[0], half_extents[1], half_extents[2]),
    };
  }
  case data::pak::physics::ShapeType::kCylinder: {
    const float radius = shape_desc.shape_params.cylinder.radius;
    const float half_height = shape_desc.shape_params.cylinder.half_height;
    ensure_no_cooked_ref();
    if (!(radius > 0.0F) || !(half_height > 0.0F)) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid cylinder params in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::CylinderShape {
      .radius = radius,
      .half_height = half_height,
    };
  }
  case data::pak::physics::ShapeType::kCone: {
    const float radius = shape_desc.shape_params.cone.radius;
    const float half_height = shape_desc.shape_params.cone.half_height;
    if (!(radius > 0.0F) || !(half_height > 0.0F)) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid cone params in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::ConeShape {
      .radius = radius,
      .half_height = half_height,
      .cooked_payload = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kConvex, binding_kind,
        node_index),
    };
  }
  case data::pak::physics::ShapeType::kConvexHull:
    return physics::ConvexHullShape {
      .cooked_payload = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kConvex, binding_kind,
        node_index),
    };
  case data::pak::physics::ShapeType::kTriangleMesh:
    return physics::TriangleMeshShape {
      .cooked_payload = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kMesh, binding_kind, node_index),
    };
  case data::pak::physics::ShapeType::kHeightField:
    return physics::HeightFieldShape {
      .cooked_payload = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kHeightField, binding_kind,
        node_index),
    };
  case data::pak::physics::ShapeType::kPlane: {
    ensure_no_cooked_ref();
    const auto& normal = shape_desc.shape_params.plane.normal;
    const Vec3 n(normal[0], normal[1], normal[2]);
    if (!(glm::dot(n, n) > 0.0F)) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid plane normal in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::PlaneShape {
      .normal = n,
      .distance = shape_desc.shape_params.plane.distance,
    };
  }
  case data::pak::physics::ShapeType::kWorldBoundary: {
    ensure_no_cooked_ref();
    const auto& params = shape_desc.shape_params.world_boundary;
    if (params.boundary_mode
        != data::pak::physics::WorldBoundaryMode::kAabbClamp
      && params.boundary_mode
        != data::pak::physics::WorldBoundaryMode::kPlaneSet) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid world_boundary boundary_mode in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    if (!(params.limits_max[0] > params.limits_min[0])
      || !(params.limits_max[1] > params.limits_min[1])
      || !(params.limits_max[2] > params.limits_min[2])) {
      throw std::runtime_error(
        std::string("OXY-SHAPE-008: invalid world_boundary limits in ")
        + std::string(binding_kind)
        + " binding (node_index=" + std::to_string(node_index) + ")");
    }
    return physics::WorldBoundaryShape {
      .mode = static_cast<physics::WorldBoundaryMode>(params.boundary_mode),
      .limits_min
      = Vec3(params.limits_min[0], params.limits_min[1], params.limits_min[2]),
      .limits_max
      = Vec3(params.limits_max[0], params.limits_max[1], params.limits_max[2]),
    };
  }
  case data::pak::physics::ShapeType::kCompound:
    return physics::CompoundShape {
      .cooked_payload = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kCompound, binding_kind,
        node_index),
    };
  default:
    throw std::runtime_error(
      std::string("OXY-SHAPE-001: unsupported shape_type in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + " shape_type="
      + std::to_string(static_cast<uint32_t>(shape_desc.shape_type)) + ")");
  }
}

auto SceneLoaderService::ResolveCookedShapePayload(
  const data::pak::physics::CollisionShapeAssetDesc& shape_desc,
  const data::pak::physics::ShapePayloadType expected_payload_type,
  const std::string_view binding_kind, const uint32_t node_index) const
  -> physics::CookedShapePayload
{
  if (!current_physics_context_) {
    throw std::runtime_error(std::string(binding_kind)
      + " cooked_shape_ref requires active physics asset context (node_index="
      + std::to_string(node_index) + ")");
  }
  const auto& cooked_ref = shape_desc.cooked_shape_ref;
  if (cooked_ref.payload_asset_key.IsNil()) {
    throw std::runtime_error(
      std::string(
        "OXY-SHAPE-004: missing cooked_shape_ref.payload_asset_key in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + ")");
  }
  if (cooked_ref.payload_type != expected_payload_type) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-006: cooked_shape_ref.payload_type mismatch in ")
      + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + " expected="
      + std::to_string(static_cast<uint32_t>(expected_payload_type))
      + " actual="
      + std::to_string(static_cast<uint32_t>(cooked_ref.payload_type)) + ")");
  }

  const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
    *current_physics_context_, cooked_ref.payload_asset_key);
  if (!resource_key_opt.has_value()) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: failed to resolve physics resource key in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " payload_asset_key="
      + data::to_string(cooked_ref.payload_asset_key) + ")");
  }

  auto resource = FindHydrationPayload(*resource_key_opt);
  if (!resource) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: physics payload resource not loaded in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " payload_asset_key="
      + data::to_string(cooked_ref.payload_asset_key) + ")");
  }

  const auto backend = ResolveActiveBackend(engine_, "shape payload hydration");
  const auto expected_format
    = ExpectedShapeResourceFormat(backend, expected_payload_type);
  if (!expected_format.has_value()) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: unsupported backend/shape payload routing ")
      + "in " + std::string(binding_kind)
      + " binding (node_index=" + std::to_string(node_index) + " backend="
      + std::to_string(static_cast<uint32_t>(backend)) + " payload_type="
      + std::to_string(static_cast<uint32_t>(expected_payload_type)) + ")");
  }

  if (resource->GetFormat() != *expected_format) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: unsupported physics resource format in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " payload_asset_key="
      + data::to_string(cooked_ref.payload_asset_key) + " format="
      + std::to_string(static_cast<uint32_t>(resource->GetFormat())) + ")");
  }

  const auto data = resource->GetData();
  std::vector<uint8_t> payload(data.begin(), data.end());
  return physics::CookedShapePayload {
    .payload_type
    = static_cast<physics::ShapePayloadType>(cooked_ref.payload_type),
    .data = std::move(payload),
  };
}

void SceneLoaderService::HydrateJointBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::JointBindingRecord> bindings)
{
  const auto world_id = physics_module.GetWorldId();
  if (!physics::IsValid(world_id)) {
    throw std::runtime_error("joint hydration requires a valid physics world");
  }
  if (!current_physics_context_) {
    throw std::runtime_error(
      "joint hydration requires active physics hydration context");
  }
  const auto backend = ResolveActiveBackend(engine_, "joint hydration");
  const auto expected_constraint_format = ExpectedJointResourceFormat(backend);
  if (!expected_constraint_format.has_value()) {
    throw std::runtime_error(
      std::string("joint hydration does not support physics backend ")
      + std::to_string(static_cast<uint32_t>(backend)));
  }

  for (const auto& record : bindings) {
    const auto node_index_a = static_cast<size_t>(record.node_index_a);
    const auto node_index_b = static_cast<size_t>(record.node_index_b);
    if (node_index_a >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("joint node_index_a out of range: ")
        + std::to_string(record.node_index_a));
    }
    if (record.node_index_b == data::pak::physics::kWorldAttachmentNodeIndex) {
      throw std::runtime_error(
        std::string("joint world-anchor mode is not supported by SceneLoader "
                    "hydrator (node_index_a=")
        + std::to_string(record.node_index_a) + ")");
    }
    if (node_index_b >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("joint node_index_b out of range: ")
        + std::to_string(record.node_index_b));
    }
    auto constraint_blob = std::span<const uint8_t> {};
    if (!record.constraint_asset_key.IsNil()) {
      const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
        *current_physics_context_, record.constraint_asset_key);
      if (!resource_key_opt.has_value()) {
        throw std::runtime_error(
          std::string("joint constraint_asset_key could not be resolved ")
          + "(node_index_a=" + std::to_string(record.node_index_a)
          + " node_index_b=" + std::to_string(record.node_index_b)
          + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
      }
      const auto constraint_resource = FindHydrationPayload(*resource_key_opt);
      if (!constraint_resource) {
        throw std::runtime_error(
          std::string("joint constraint resource is not loaded ")
          + "(node_index_a=" + std::to_string(record.node_index_a)
          + " node_index_b=" + std::to_string(record.node_index_b)
          + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
      }
      if (constraint_resource->GetFormat() != *expected_constraint_format) {
        throw std::runtime_error(
          std::string("joint constraint resource format is not ")
          + std::to_string(static_cast<uint32_t>(*expected_constraint_format))
          + " (node_index_a=" + std::to_string(record.node_index_a)
          + " node_index_b=" + std::to_string(record.node_index_b)
          + " asset_key=" + data::to_string(record.constraint_asset_key)
          + " format="
          + std::to_string(
            static_cast<uint32_t>(constraint_resource->GetFormat()))
          + ")");
      }
      if (constraint_resource->GetData().empty()) {
        throw std::runtime_error(
          std::string("joint constraint resource payload is empty ")
          + "(node_index_a=" + std::to_string(record.node_index_a)
          + " node_index_b=" + std::to_string(record.node_index_b)
          + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
      }
      constraint_blob = constraint_resource->GetData();
    } else {
      throw std::runtime_error(
        std::string("joint constraint_asset_key is missing ")
        + "(node_index_a=" + std::to_string(record.node_index_a)
        + " node_index_b=" + std::to_string(record.node_index_b) + ")");
    }

    auto body_a = physics::ScenePhysics::GetRigidBody(
      observer_ptr<physics::PhysicsModule> { &physics_module },
      runtime_nodes_.at(node_index_a).GetHandle());
    if (!body_a.has_value()) {
      throw std::runtime_error(
        std::string("joint node_index_a does not have a rigid body: ")
        + std::to_string(record.node_index_a));
    }
    auto body_b = physics::ScenePhysics::GetRigidBody(
      observer_ptr<physics::PhysicsModule> { &physics_module },
      runtime_nodes_.at(node_index_b).GetHandle());
    if (!body_b.has_value()) {
      throw std::runtime_error(
        std::string("joint node_index_b does not have a rigid body: ")
        + std::to_string(record.node_index_b));
    }

    physics::joint::JointDesc desc {};
    desc.type = physics::joint::JointType::kFixed;
    desc.body_a = *body_a;
    desc.body_b = *body_b;
    desc.constraint_settings_blob = constraint_blob;

    const auto created = physics_module.Joints().CreateJoint(world_id, desc);
    if (!created.has_value()) {
      throw std::runtime_error(
        std::string("failed to create joint binding (node_index_a=")
        + std::to_string(record.node_index_a)
        + " node_index_b=" + std::to_string(record.node_index_b) + ")");
    }
  }
}

void SceneLoaderService::HydrateColliderBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::ColliderBindingRecord> bindings)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("collider node_index out of range: ")
        + std::to_string(record.node_index));
    }
    if (record.is_sensor != 0U && record.is_sensor != 1U) {
      throw std::runtime_error(std::string("collider is_sensor must be 0 or 1 ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }

    physics::body::BodyDesc desc {};
    desc.type = physics::body::BodyType::kStatic;
    // Colliders are trigger-only volumes by design (no contact impulses).
    // Keep this invariant even when authoring omits explicit sensor flags.
    desc.flags = physics::body::BodyFlags::kIsTrigger;
    desc.gravity_factor = 0.0F;
    desc.mass_kg = 1.0F;
    desc.linear_damping = 0.0F;
    desc.angular_damping = 0.0F;
    desc.collision_layer = physics::CollisionLayer { static_cast<uint32_t>(
      record.collision_layer) };
    desc.collision_mask
      = physics::CollisionMask { static_cast<uint32_t>(record.collision_mask) };
    if (!record.shape_asset_key.IsNil()) {
      const auto shape_desc = ResolveCollisionShapeAsset(
        record.shape_asset_key, "collider", record.node_index);
      desc.shape = BuildCollisionShapeFromDescriptor(
        shape_desc, "collider", record.node_index);
      desc.shape_local_position = Vec3(shape_desc.local_position[0],
        shape_desc.local_position[1], shape_desc.local_position[2]);
      desc.shape_local_rotation
        = Quat(shape_desc.local_rotation[3], shape_desc.local_rotation[0],
          shape_desc.local_rotation[1], shape_desc.local_rotation[2]);
      desc.shape_local_scale = Vec3(shape_desc.local_scale[0],
        shape_desc.local_scale[1], shape_desc.local_scale[2]);
      if (!shape_desc.material_asset_key.IsNil()) {
        const auto material_desc = ResolvePhysicsMaterialAsset(
          shape_desc.material_asset_key, "collider", record.node_index);
        const auto effective_friction
          = (std::max)(material_desc.static_friction,
            material_desc.dynamic_friction);
        desc.friction = (std::max)(0.0F, effective_friction);
        desc.restitution = (std::max)(0.0F, material_desc.restitution);
      }
    }
    const auto [initial_position, initial_rotation]
      = ReadHydrationWorldPose(node_index);
    desc.initial_position = initial_position;
    desc.initial_rotation = initial_rotation;
    auto& node = runtime_nodes_.at(node_index);
    const auto attached = physics::ScenePhysics::AttachRigidBodyDetailed(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      const auto reason_text
        = std::string(physics::to_string(attached.error()));
      throw std::runtime_error(
        std::string("failed to attach collider binding for node_index=")
        + std::to_string(record.node_index) + " reason=" + reason_text);
    }
    LOG_F(INFO,
      "SceneLoader: provisioned trigger collider (node_index={} body_id={} "
      "shape_key={} collision_layer={} collision_mask={} is_sensor={}).",
      record.node_index, attached->get(),
      data::to_string(record.shape_asset_key), record.collision_layer,
      record.collision_mask, record.is_sensor);
  }
}

void SceneLoaderService::HydrateRigidBodyBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::RigidBodyBindingRecord> bindings,
  const std::unordered_set<uint32_t>& ccd_forbidden_node_indices)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("rigid-body node_index out of range: ")
        + std::to_string(record.node_index));
    }
    if (record.is_sensor != 0U && record.is_sensor != 1U) {
      throw std::runtime_error(
        std::string("rigid-body is_sensor must be 0 or 1 (node_index=")
        + std::to_string(record.node_index) + ")");
    }

    physics::body::BodyDesc desc {};
    switch (record.body_type) {
    case data::pak::physics::PhysicsBodyType::kStatic:
      desc.type = physics::body::BodyType::kStatic;
      break;
    case data::pak::physics::PhysicsBodyType::kDynamic:
      desc.type = physics::body::BodyType::kDynamic;
      break;
    case data::pak::physics::PhysicsBodyType::kKinematic:
      desc.type = physics::body::BodyType::kKinematic;
      break;
    default:
      throw std::runtime_error(
        std::string("unsupported rigid-body type value: ")
        + std::to_string(static_cast<uint32_t>(record.body_type)));
    }

    desc.flags = physics::body::BodyFlags::kNone;
    desc.gravity_factor = (std::max)(0.0F, record.gravity_factor);
    auto is_sensor = record.is_sensor != 0U;
    if (desc.gravity_factor > 0.0F) {
      desc.flags = desc.flags | physics::body::BodyFlags::kEnableGravity;
    }
    if (record.motion_quality
      == data::pak::physics::PhysicsMotionQuality::kLinearCast) {
      if (!ccd_forbidden_node_indices.contains(record.node_index)) {
        desc.flags = desc.flags
          | physics::body::BodyFlags::kEnableContinuousCollisionDetection;
      } else {
        LOG_F(WARNING,
          "SceneLoader: Coerced rigid-body motion quality to discrete for "
          "node_index={} due to runtime stability constraints.",
          record.node_index);
      }
    }
    desc.mass_kg = record.mass > 0.0F ? record.mass : 1.0F;
    desc.linear_damping = (std::max)(0.0F, record.linear_damping);
    desc.angular_damping = (std::max)(0.0F, record.angular_damping);
    desc.collision_layer = physics::CollisionLayer { static_cast<uint32_t>(
      record.collision_layer) };
    desc.collision_mask
      = physics::CollisionMask { static_cast<uint32_t>(record.collision_mask) };
    std::optional<data::pak::physics::CollisionShapeAssetDesc>
      shape_desc_opt {};
    if (!record.shape_asset_key.IsNil()) {
      const auto shape_desc = ResolveCollisionShapeAsset(
        record.shape_asset_key, "rigid-body", record.node_index);
      if (desc.type == physics::body::BodyType::kDynamic
        && (shape_desc.shape_type
            == data::pak::physics::ShapeType::kTriangleMesh
          || shape_desc.shape_type
            == data::pak::physics::ShapeType::kHeightField
          || shape_desc.shape_type == data::pak::physics::ShapeType::kPlane
          || shape_desc.shape_type
            == data::pak::physics::ShapeType::kWorldBoundary)) {
        throw std::runtime_error(
          std::string(
            "OXY-SHAPE-008: shape_type is not valid for dynamic body in ")
          + "rigid-body binding (node_index="
          + std::to_string(record.node_index) + " shape_type="
          + std::to_string(static_cast<uint32_t>(shape_desc.shape_type)) + ")");
      }
      shape_desc_opt = shape_desc;
      is_sensor = is_sensor || shape_desc.is_sensor != 0U;
      desc.shape = BuildCollisionShapeFromDescriptor(
        shape_desc, "rigid-body", record.node_index);
      desc.shape_local_position = Vec3(shape_desc.local_position[0],
        shape_desc.local_position[1], shape_desc.local_position[2]);
      desc.shape_local_rotation
        = Quat(shape_desc.local_rotation[3], shape_desc.local_rotation[0],
          shape_desc.local_rotation[1], shape_desc.local_rotation[2]);
      desc.shape_local_scale = Vec3(shape_desc.local_scale[0],
        shape_desc.local_scale[1], shape_desc.local_scale[2]);
      if (!shape_desc.material_asset_key.IsNil()) {
        const auto material_desc = ResolvePhysicsMaterialAsset(
          shape_desc.material_asset_key, "rigid-body", record.node_index);
        const auto effective_friction
          = (std::max)(material_desc.static_friction,
            material_desc.dynamic_friction);
        desc.friction = (std::max)(0.0F, effective_friction);
        desc.restitution = (std::max)(0.0F, material_desc.restitution);
        if (record.mass <= 0.0F
          && desc.type != physics::body::BodyType::kStatic) {
          desc.mass_kg = (std::max)(kMinDynamicMassKg, material_desc.density);
        }
      }
    }
    if (is_sensor) {
      desc.flags = desc.flags | physics::body::BodyFlags::kIsTrigger;
    }

    const auto [initial_position, initial_rotation]
      = ReadHydrationWorldPose(node_index);
    desc.initial_position = initial_position;
    desc.initial_rotation = initial_rotation;
    auto& node = runtime_nodes_.at(node_index);
    const auto attached = physics::ScenePhysics::AttachRigidBodyDetailed(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      const auto reason_text
        = std::string(physics::to_string(attached.error()));
      const auto format_quat = [](const Quat& q) -> std::string {
        return std::string("(") + std::to_string(q.w) + ", "
          + std::to_string(q.x) + ", " + std::to_string(q.y) + ", "
          + std::to_string(q.z) + ")";
      };
      if (shape_desc_opt.has_value()) {
        const auto& shape_desc = *shape_desc_opt;
        throw std::runtime_error(
          std::string("failed to attach rigid-body binding for node_index=")
          + std::to_string(record.node_index) + " shape_asset_key="
          + data::to_string(record.shape_asset_key) + " shape_type="
          + std::to_string(static_cast<uint32_t>(shape_desc.shape_type))
          + " cooked_ref_asset_key="
          + data::to_string(shape_desc.cooked_shape_ref.payload_asset_key)
          + " cooked_ref_payload_type="
          + std::to_string(
            static_cast<uint32_t>(shape_desc.cooked_shape_ref.payload_type))
          + " initial_rotation=" + format_quat(desc.initial_rotation)
          + " shape_local_rotation=" + format_quat(desc.shape_local_rotation)
          + " reason=" + reason_text);
      }
      throw std::runtime_error(
        std::string("failed to attach rigid-body binding for node_index=")
        + std::to_string(record.node_index) + " initial_rotation="
        + format_quat(desc.initial_rotation) + " shape_local_rotation="
        + format_quat(desc.shape_local_rotation) + " reason=" + reason_text);
    }
  }
}

void SceneLoaderService::HydrateCharacterBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::CharacterBindingRecord> bindings)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("character node_index out of range: ")
        + std::to_string(record.node_index));
    }

    physics::character::CharacterDesc desc {};
    desc.mass_kg = record.mass;
    desc.max_slope_angle_radians = record.max_slope_angle;
    desc.max_strength = record.max_strength;
    desc.predictive_contact_distance = record.step_height;
    desc.collision_layer = physics::CollisionLayer { static_cast<uint32_t>(
      record.collision_layer) };
    desc.collision_mask
      = physics::CollisionMask { static_cast<uint32_t>(record.collision_mask) };
    if (!record.shape_asset_key.IsNil()) {
      const auto shape_desc = ResolveCollisionShapeAsset(
        record.shape_asset_key, "character", record.node_index);
      desc.shape = BuildCollisionShapeFromDescriptor(
        shape_desc, "character", record.node_index);
      desc.shape_local_position = Vec3(shape_desc.local_position[0],
        shape_desc.local_position[1], shape_desc.local_position[2]);
      desc.shape_local_rotation
        = Quat(shape_desc.local_rotation[3], shape_desc.local_rotation[0],
          shape_desc.local_rotation[1], shape_desc.local_rotation[2]);
      desc.shape_local_scale = Vec3(shape_desc.local_scale[0],
        shape_desc.local_scale[1], shape_desc.local_scale[2]);
    }

    const auto [initial_position, initial_rotation]
      = ReadHydrationWorldPose(node_index);
    desc.initial_position = initial_position;
    desc.initial_rotation = initial_rotation;
    auto& node = runtime_nodes_.at(node_index);
    const auto attached = physics::ScenePhysics::AttachCharacterDetailed(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      const auto reason_text
        = std::string(physics::to_string(attached.error()));
      throw std::runtime_error(
        std::string("failed to attach character binding for node_index=")
        + std::to_string(record.node_index) + " reason=" + reason_text);
    }
  }
}

void SceneLoaderService::HydrateSoftBodyBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::SoftBodyBindingRecord> bindings)
{
  const auto world_id = physics_module.GetWorldId();
  if (!physics::IsValid(world_id)) {
    throw std::runtime_error(
      "soft-body hydration requires a valid physics world");
  }
  if (!current_physics_context_) {
    throw std::runtime_error(
      "soft-body hydration requires active physics hydration context");
  }

  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("soft-body node_index out of range: ")
        + std::to_string(record.node_index));
    }
    const auto backend = ResolveActiveBackend(engine_, "soft-body hydration");
    const auto use_jolt_settings = backend == EnginePhysicsBackend::kJolt;
    const auto use_physx_settings = backend == EnginePhysicsBackend::kPhysX;
    if (!use_jolt_settings && !use_physx_settings) {
      throw std::runtime_error(
        std::string("soft-body hydration does not support physics backend ")
        + std::to_string(static_cast<uint32_t>(backend)));
    }

    if (record.topology_asset_key.IsNil()) {
      throw std::runtime_error(
        std::string("soft-body topology_asset_key is missing ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }

    const auto selected_settings_asset_key = record.topology_asset_key;
    const auto expected_settings_format = use_jolt_settings
      ? data::pak::physics::PhysicsResourceFormat::
          kJoltSoftBodySharedSettingsBinary
      : data::pak::physics::PhysicsResourceFormat::kPhysXSoftBodySettingsBinary;
    const auto* const expected_settings_format_name = use_jolt_settings
      ? "jolt_soft_body_shared_settings_binary"
      : "physx_soft_body_settings_binary";
    const auto* const selected_backend_name
      = use_jolt_settings ? "jolt" : "physx";
    if (record.topology_format != expected_settings_format) {
      throw std::runtime_error(
        std::string("soft-body topology_format does not match active backend ")
        + "(node_index=" + std::to_string(record.node_index)
        + " topology_format="
        + std::to_string(static_cast<uint32_t>(record.topology_format))
        + " backend=" + selected_backend_name + ")");
    }

    const auto validate_non_negative_finite
      = [&](const float value, const std::string_view field) -> void {
      if (!std::isfinite(value) || value < 0.0F) {
        throw std::runtime_error(std::string("soft-body ") + std::string(field)
          + " must be finite and >= 0 (node_index="
          + std::to_string(record.node_index) + ")");
      }
    };
    validate_non_negative_finite(record.edge_compliance, "edge_compliance");
    validate_non_negative_finite(record.shear_compliance, "shear_compliance");
    validate_non_negative_finite(record.bend_compliance, "bend_compliance");
    validate_non_negative_finite(record.volume_compliance, "volume_compliance");
    validate_non_negative_finite(record.global_damping, "global_damping");
    if (!std::isfinite(record.pressure_coefficient)) {
      throw std::runtime_error(
        std::string(
          "soft-body pressure_coefficient must be finite (node_index=")
        + std::to_string(record.node_index) + ")");
    }
    if (!std::isfinite(record.tether_max_distance_multiplier)
      || record.tether_max_distance_multiplier < 0.0F) {
      throw std::runtime_error(
        std::string(
          "soft-body tether_max_distance_multiplier must be finite and >= 0 "
          "(node_index=")
        + std::to_string(record.node_index) + ")");
    }
    if (record.solver_iteration_count == 0U) {
      throw std::runtime_error(
        std::string("soft-body solver_iteration_count must be > 0 (node_index=")
        + std::to_string(record.node_index) + ")");
    }
    auto runtime_solver_iterations = record.solver_iteration_count;
    auto runtime_gravity_factor = 1.0F;
    if (use_jolt_settings) {
      runtime_solver_iterations = (std::max)(runtime_solver_iterations,
        record.backend_scalars.jolt.num_position_steps);
      runtime_solver_iterations = (std::max)(runtime_solver_iterations,
        record.backend_scalars.jolt.num_velocity_steps);
      runtime_gravity_factor = record.backend_scalars.jolt.gravity_factor;
      if (!std::isfinite(runtime_gravity_factor)) {
        throw std::runtime_error(
          std::string("soft-body backend gravity_factor must be finite "
                      "(node_index=")
          + std::to_string(record.node_index) + ")");
      }
    }
    validate_non_negative_finite(record.restitution, "restitution");
    validate_non_negative_finite(record.friction, "friction");
    validate_non_negative_finite(record.vertex_radius, "vertex_radius");

    const auto settings_resource_key_opt
      = loader_.MakePhysicsResourceKeyForAsset(
        *current_physics_context_, selected_settings_asset_key);
    if (!settings_resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string("soft-body selected settings resource key could not be "
                    "resolved ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(selected_settings_asset_key)
        + " backend=" + selected_backend_name + ")");
    }
    const auto settings_resource
      = FindHydrationPayload(*settings_resource_key_opt);
    if (!settings_resource) {
      throw std::runtime_error(
        std::string("soft-body settings resource is not loaded ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(selected_settings_asset_key)
        + " backend=" + selected_backend_name + ")");
    }
    if (settings_resource->GetFormat() != expected_settings_format) {
      throw std::runtime_error(
        std::string("soft-body settings resource format is not ")
        + expected_settings_format_name
        + " (node_index=" + std::to_string(record.node_index) + " asset_key="
        + data::to_string(selected_settings_asset_key) + " format="
        + std::to_string(static_cast<uint32_t>(settings_resource->GetFormat()))
        + " backend=" + selected_backend_name + ")");
    }
    if (settings_resource->GetData().empty()) {
      throw std::runtime_error(
        std::string("soft-body settings resource payload is empty ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(selected_settings_asset_key)
        + " backend=" + selected_backend_name + ")");
    }

    auto desc = physics::softbody::SoftBodyDesc {};
    desc.cluster_count = (std::max)(record.solver_iteration_count, 1U);
    desc.material_params.stiffness = 0.0F;
    desc.material_params.damping = record.global_damping;
    desc.material_params.edge_compliance = record.edge_compliance;
    desc.material_params.shear_compliance = record.shear_compliance;
    desc.material_params.bend_compliance = record.bend_compliance;
    desc.material_params.volume_compliance = record.volume_compliance;
    desc.material_params.pressure_coefficient = record.pressure_coefficient;
    desc.material_params.tether_mode
      = ToRuntimeSoftBodyTetherMode(record.tether_mode);
    desc.material_params.tether_max_distance_multiplier
      = record.tether_max_distance_multiplier;
    desc.restitution = record.restitution;
    desc.friction = record.friction;
    desc.vertex_radius = record.vertex_radius;
    desc.solver_iteration_count = runtime_solver_iterations;
    desc.gravity_factor = runtime_gravity_factor;
    const auto [initial_position, initial_rotation]
      = ReadHydrationWorldPose(node_index);
    auto& node = runtime_nodes_.at(node_index);
    desc.settings_scale = oxygen::Vec3 { 1.0F, 1.0F, 1.0F };
    desc.collision_layer = physics::CollisionLayer { static_cast<uint32_t>(
      record.collision_layer) };
    desc.collision_mask
      = physics::CollisionMask { static_cast<uint32_t>(record.collision_mask) };
    desc.initial_position = initial_position;
    desc.initial_rotation = initial_rotation;
    desc.settings_blob = settings_resource->GetData();

    const auto created
      = physics_module.SoftBodies().CreateSoftBody(world_id, desc);
    if (!created.has_value()) {
      throw std::runtime_error(
        std::string("failed to attach soft-body binding for node_index=")
        + std::to_string(record.node_index)
        + " reason=" + std::string(physics::to_string(created.error())));
    }

    const auto soft_body_id = created.value();
    const auto authority_result
      = physics_module.SoftBodies().GetAuthority(world_id, soft_body_id);
    if (!authority_result.has_value()) {
      (void)physics_module.SoftBodies().DestroySoftBody(world_id, soft_body_id);
      throw std::runtime_error(
        std::string("failed to resolve soft-body authority for node_index=")
        + std::to_string(record.node_index) + " reason="
        + std::string(physics::to_string(authority_result.error())));
    }

    const auto node_handle = node.GetHandle();
    if (physics::ScenePhysics::GetRigidBody(
          observer_ptr<physics::PhysicsModule> { &physics_module }, node_handle)
          .has_value()) {
      (void)physics_module.SoftBodies().DestroySoftBody(world_id, soft_body_id);
      throw std::runtime_error(
        std::string("soft-body node already has a rigid body mapping ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    if (physics::ScenePhysics::GetCharacter(
          observer_ptr<physics::PhysicsModule> { &physics_module }, node_handle)
          .has_value()) {
      (void)physics_module.SoftBodies().DestroySoftBody(world_id, soft_body_id);
      throw std::runtime_error(
        std::string("soft-body node already has a character mapping ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    if (physics_module.HasAggregateForNode(node_handle)) {
      (void)physics_module.SoftBodies().DestroySoftBody(world_id, soft_body_id);
      throw std::runtime_error(
        std::string("soft-body node already has an aggregate mapping ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    if (const auto existing_node
      = physics_module.GetNodeForAggregateId(soft_body_id);
      existing_node.has_value() && *existing_node != node_handle) {
      (void)physics_module.SoftBodies().DestroySoftBody(world_id, soft_body_id);
      throw std::runtime_error(
        std::string("soft-body aggregate id collides with existing mapping id=")
        + physics::to_string(soft_body_id)
        + " (node_index=" + std::to_string(record.node_index) + ")");
    }

    physics_module.RegisterNodeAggregateMapping(
      node_handle, soft_body_id, authority_result.value());
    const auto node_name = node.GetName();
    LOG_F(INFO,
      "SceneLoader: hydrated soft body binding (node_index={} node='{}' "
      "aggregate_id={} backend={} topology_asset_key={} "
      "topology_format={} solver_iteration_count={} self_collision={} "
      "restitution={:.3f} friction={:.3f} vertex_radius={:.3f} "
      "settings_scale=[{:.3f},{:.3f},{:.3f}]).",
      record.node_index, node_name.c_str(), soft_body_id.get(),
      selected_backend_name, data::to_string(selected_settings_asset_key),
      static_cast<uint32_t>(record.topology_format),
      record.solver_iteration_count,
      static_cast<uint32_t>(record.self_collision), record.restitution,
      record.friction, record.vertex_radius, desc.settings_scale.x,
      desc.settings_scale.y, desc.settings_scale.z);
  }
}

void SceneLoaderService::HydrateVehicleBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::VehicleBindingRecord> bindings,
  const std::span<const data::pak::physics::VehicleWheelBindingRecord>
    wheel_bindings)
{
  const auto world_id = physics_module.GetWorldId();
  if (!physics::IsValid(world_id)) {
    throw std::runtime_error(
      "vehicle hydration requires a valid physics world");
  }
  if (!current_physics_context_) {
    throw std::runtime_error(
      "vehicle hydration requires active physics hydration context");
  }
  const auto backend = ResolveActiveBackend(engine_, "vehicle hydration");
  const auto expected_constraint_format
    = ExpectedVehicleResourceFormat(backend);
  if (!expected_constraint_format.has_value()) {
    throw std::runtime_error(
      std::string("vehicle hydration does not support physics backend ")
      + std::to_string(static_cast<uint32_t>(backend)));
  }

  for (const auto& record : bindings) {
    const auto chassis_node_index = static_cast<size_t>(record.node_index);
    if (chassis_node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("vehicle node_index out of range: ")
        + std::to_string(record.node_index));
    }
    if (record.controller_type
        != data::pak::physics::VehicleControllerType::kWheeled
      && record.controller_type
        != data::pak::physics::VehicleControllerType::kTracked) {
      throw std::runtime_error(
        std::string("vehicle controller_type has invalid value ")
        + "(node_index=" + std::to_string(record.node_index)
        + " controller_type="
        + std::to_string(static_cast<uint32_t>(record.controller_type)) + ")");
    }
    if (record.constraint_asset_key.IsNil()) {
      throw std::runtime_error(
        std::string("vehicle constraint_asset_key is missing for ")
        + "node_index=" + std::to_string(record.node_index));
    }

    const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
      *current_physics_context_, record.constraint_asset_key);
    if (!resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string("vehicle constraint_asset_key could not be resolved ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
    }
    const auto constraint_resource = FindHydrationPayload(*resource_key_opt);
    if (!constraint_resource) {
      throw std::runtime_error(
        std::string("vehicle constraint resource is not loaded ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
    }
    if (constraint_resource->GetFormat() != *expected_constraint_format) {
      throw std::runtime_error(
        std::string("vehicle constraint resource format is not ")
        + std::to_string(static_cast<uint32_t>(*expected_constraint_format))
        + " (node_index=" + std::to_string(record.node_index) + " asset_key="
        + data::to_string(record.constraint_asset_key) + " format="
        + std::to_string(
          static_cast<uint32_t>(constraint_resource->GetFormat()))
        + ")");
    }
    if (constraint_resource->GetData().empty()) {
      throw std::runtime_error(
        std::string("vehicle constraint resource payload is empty ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
    }

    const auto chassis_body = physics::ScenePhysics::GetRigidBody(
      observer_ptr<physics::PhysicsModule> { &physics_module },
      runtime_nodes_.at(chassis_node_index).GetHandle());
    if (!chassis_body.has_value()) {
      throw std::runtime_error(
        std::string("vehicle chassis node does not have a rigid body ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    const auto chassis_body_type
      = physics_module.GetBodyTypeForBodyId(*chassis_body);
    if (!chassis_body_type.has_value()
      || *chassis_body_type != physics::body::BodyType::kDynamic) {
      throw std::runtime_error(
        std::string("vehicle chassis body must be dynamic ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    const auto chassis_position_result
      = physics_module.Bodies().GetBodyPosition(world_id, *chassis_body);
    if (!chassis_position_result.has_value()
      || !IsFiniteVec3(chassis_position_result.value())) {
      throw std::runtime_error(
        std::string("vehicle chassis pose could not be resolved ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    const auto chassis_position = chassis_position_result.value();

    const auto wheel_slice_offset
      = static_cast<size_t>(record.wheel_slice_offset);
    const auto wheel_slice_count
      = static_cast<size_t>(record.wheel_slice_count);
    const auto min_wheel_count = record.controller_type
        == data::pak::physics::VehicleControllerType::kTracked
      ? 4U
      : 2U;
    if (wheel_slice_count < min_wheel_count) {
      throw std::runtime_error(
        std::string("vehicle wheel_slice_count below controller minimum ")
        + "(node_index=" + std::to_string(record.node_index)
        + " controller_type="
        + std::to_string(static_cast<uint32_t>(record.controller_type))
        + " min_required=" + std::to_string(min_wheel_count)
        + " wheel_slice_count=" + std::to_string(wheel_slice_count) + ")");
    }
    if (wheel_slice_offset > wheel_bindings.size()
      || (wheel_bindings.size() - wheel_slice_offset) < wheel_slice_count) {
      throw std::runtime_error(
        std::string("vehicle wheel slice is out of range ")
        + "(node_index=" + std::to_string(record.node_index)
        + " wheel_slice_offset=" + std::to_string(wheel_slice_offset)
        + " wheel_slice_count=" + std::to_string(wheel_slice_count)
        + " wheel_table_size=" + std::to_string(wheel_bindings.size()) + ")");
    }

    auto wheel_records_for_vehicle
      = std::vector<data::pak::physics::VehicleWheelBindingRecord> {};
    wheel_records_for_vehicle.reserve(wheel_slice_count);
    for (size_t i = 0; i < wheel_slice_count; ++i) {
      const auto& wheel_binding
        = oxygen::base::CheckedAt(wheel_bindings, wheel_slice_offset + i);
      if (wheel_binding.vehicle_node_index != record.node_index) {
        throw std::runtime_error(
          std::string("vehicle wheel slice references mismatched chassis node ")
          + "(node_index=" + std::to_string(record.node_index)
          + " wheel_table_index=" + std::to_string(wheel_slice_offset + i)
          + " wheel_vehicle_node_index="
          + std::to_string(wheel_binding.vehicle_node_index) + ")");
      }
      wheel_records_for_vehicle.push_back(wheel_binding);
    }

    auto wheel_desc_storage
      = std::vector<physics::vehicle::VehicleWheelDesc> {};
    wheel_desc_storage.reserve(wheel_records_for_vehicle.size());
    auto wheel_positions_storage = std::vector<Vec3> {};
    wheel_positions_storage.reserve(wheel_records_for_vehicle.size());
    auto distinct_wheel_ids = std::unordered_set<physics::BodyId> {};
    distinct_wheel_ids.reserve(wheel_records_for_vehicle.size());
    constexpr float kMinWheelOffsetSq = 1.0e-6F;
    for (const auto& wheel_binding : wheel_records_for_vehicle) {
      const auto wheel_node_index_u32 = wheel_binding.wheel_node_index;
      const auto wheel_node_index
        = static_cast<size_t>(wheel_binding.wheel_node_index);
      if (wheel_node_index >= runtime_nodes_.size()) {
        throw std::runtime_error(
          std::string("vehicle wheel node_index out of range: ")
          + std::to_string(wheel_node_index_u32));
      }

      const auto wheel_body = physics::ScenePhysics::GetRigidBody(
        observer_ptr<physics::PhysicsModule> { &physics_module },
        runtime_nodes_.at(wheel_node_index).GetHandle());
      if (!wheel_body.has_value()) {
        throw std::runtime_error(
          std::string("vehicle wheel node does not have a rigid body ")
          + "(chassis_node_index=" + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32) + ")");
      }
      if (*wheel_body == *chassis_body) {
        throw std::runtime_error(
          std::string("vehicle wheel must not resolve to chassis body ")
          + "(chassis_node_index=" + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32) + ")");
      }
      const auto wheel_body_type
        = physics_module.GetBodyTypeForBodyId(*wheel_body);
      if (!wheel_body_type.has_value()
        || (*wheel_body_type != physics::body::BodyType::kDynamic
          && *wheel_body_type != physics::body::BodyType::kKinematic)) {
        throw std::runtime_error(
          std::string("vehicle wheel body must be dynamic or kinematic ")
          + "(chassis_node_index=" + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32) + ")");
      }
      const auto wheel_position_result
        = physics_module.Bodies().GetBodyPosition(world_id, *wheel_body);
      if (!wheel_position_result.has_value()
        || !IsFiniteVec3(wheel_position_result.value())) {
        throw std::runtime_error(
          std::string("vehicle wheel pose could not be resolved ")
          + "(chassis_node_index=" + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32)
          + " body_id=" + physics::to_string(*wheel_body) + ")");
      }
      const auto wheel_position = wheel_position_result.value();
      const auto wheel_offset = wheel_position - chassis_position;
      if (glm::dot(wheel_offset, wheel_offset) <= kMinWheelOffsetSq) {
        throw std::runtime_error(
          std::string("vehicle wheel pose overlaps chassis pose; author ")
          + "non-zero wheel node offsets (chassis_node_index="
          + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32) + ")");
      }
      for (const auto& existing_wheel_position : wheel_positions_storage) {
        const auto delta = wheel_position - existing_wheel_position;
        if (glm::dot(delta, delta) <= kMinWheelOffsetSq) {
          throw std::runtime_error(
            std::string("vehicle wheel poses overlap each other; author ")
            + "distinct wheel node offsets (chassis_node_index="
            + std::to_string(record.node_index) + " wheel_node_index="
            + std::to_string(wheel_node_index_u32) + ")");
        }
      }
      if (!distinct_wheel_ids.insert(*wheel_body).second) {
        throw std::runtime_error(
          std::string("vehicle wheel topology contains duplicate wheel body ")
          + "(chassis_node_index=" + std::to_string(record.node_index)
          + " wheel_node_index=" + std::to_string(wheel_node_index_u32)
          + " body_id=" + physics::to_string(*wheel_body) + ")");
      }
      wheel_desc_storage.push_back(physics::vehicle::VehicleWheelDesc {
        .body_id = *wheel_body,
        .axle_index = wheel_binding.axle_index,
        .side = ToRuntimeVehicleWheelSide(wheel_binding.side),
      });
      wheel_positions_storage.push_back(wheel_position);
    }

    if (wheel_desc_storage.size() < 2U) {
      throw std::runtime_error(
        std::string("vehicle wheel body topology must resolve to at least two ")
        + "distinct wheel bodies (chassis_node_index="
        + std::to_string(record.node_index) + ")");
    }

    auto vehicle_desc = physics::vehicle::VehicleDesc {
      .chassis_body_id = *chassis_body,
      .wheels
      = std::span<const physics::vehicle::VehicleWheelDesc>(wheel_desc_storage),
      .constraint_settings_blob = constraint_resource->GetData(),
      .controller_type = ToRuntimeVehicleControllerType(record.controller_type),
    };
    const auto created
      = physics_module.Vehicles().CreateVehicle(world_id, vehicle_desc);
    if (!created.has_value()) {
      throw std::runtime_error(
        std::string("failed to attach vehicle binding for node_index=")
        + std::to_string(record.node_index)
        + " reason=" + std::string(physics::to_string(created.error())));
    }

    const auto authority
      = physics_module.Vehicles().GetAuthority(world_id, created.value());
    if (!authority.has_value()) {
      (void)physics_module.Vehicles().DestroyVehicle(world_id, created.value());
      throw std::runtime_error(
        std::string("failed to resolve vehicle authority for node_index=")
        + std::to_string(record.node_index)
        + " reason=" + std::string(physics::to_string(authority.error())));
    }

    const auto mapping_node_handle
      = runtime_nodes_.at(chassis_node_index).GetHandle();
    if (const auto existing_node
      = physics_module.GetNodeForAggregateId(created.value());
      existing_node.has_value() && *existing_node != mapping_node_handle) {
      throw std::runtime_error(
        std::string("vehicle aggregate id collides with existing mapping id=")
        + physics::to_string(created.value())
        + " (node_index=" + std::to_string(record.node_index) + ")");
    }

    physics_module.RegisterNodeAggregateMapping(
      mapping_node_handle, created.value(), authority.value());
    if (!physics_module.HasAggregateForNode(mapping_node_handle)
      || physics_module.GetAggregateIdForNode(mapping_node_handle)
        != created.value()) {
      throw std::runtime_error(
        std::string("failed to register vehicle aggregate mapping ")
        + "(node_index=" + std::to_string(record.node_index)
        + " aggregate_id=" + physics::to_string(created.value()) + ")");
    }
  }
}

void SceneLoaderService::HydrateAggregateBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::AggregateBindingRecord> bindings,
  const std::span<const data::pak::physics::RigidBodyBindingRecord>
    rigid_body_bindings)
{
  const auto world_id = physics_module.GetWorldId();
  if (!physics::IsValid(world_id)) {
    throw std::runtime_error(
      "aggregate hydration requires a valid physics world");
  }

  auto& aggregate_api = physics_module.Aggregates();
  for (const auto& record : bindings) {
    const auto root_node_index = static_cast<size_t>(record.node_index);
    if (root_node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("aggregate node_index out of range: ")
        + std::to_string(record.node_index));
    }

    auto& root_node = runtime_nodes_.at(root_node_index);
    if (physics::ScenePhysics::GetRigidBody(
          observer_ptr<physics::PhysicsModule> { &physics_module },
          root_node.GetHandle())
          .has_value()) {
      throw std::runtime_error(
        std::string("aggregate root node must not have a rigid body ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    if (physics::ScenePhysics::GetCharacter(
          observer_ptr<physics::PhysicsModule> { &physics_module },
          root_node.GetHandle())
          .has_value()) {
      throw std::runtime_error(
        std::string("aggregate root node must not have a character ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }
    if (physics_module.HasAggregateForNode(root_node.GetHandle())) {
      throw std::runtime_error(
        std::string("aggregate root node already has aggregate mapping ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }

    auto member_node_indices = CollectSubtreeRigidBodyNodeIndices(
      rigid_body_bindings, runtime_nodes_, record.node_index);
    if (member_node_indices.empty()) {
      throw std::runtime_error(
        std::string("aggregate requires at least one rigid body in subtree ")
        + "(node_index=" + std::to_string(record.node_index) + ")");
    }

    auto member_body_ids = std::vector<physics::BodyId> {};
    member_body_ids.reserve(member_node_indices.size());
    for (const auto member_node_index_u32 : member_node_indices) {
      const auto member_node_index = static_cast<size_t>(member_node_index_u32);
      if (member_node_index >= runtime_nodes_.size()) {
        throw std::runtime_error(
          std::string("aggregate member node_index out of range: ")
          + std::to_string(member_node_index_u32));
      }

      const auto rigid_body = physics::ScenePhysics::GetRigidBody(
        observer_ptr<physics::PhysicsModule> { &physics_module },
        runtime_nodes_.at(member_node_index).GetHandle());
      if (!rigid_body.has_value()) {
        throw std::runtime_error(
          std::string("aggregate member node does not have a rigid body ")
          + "(aggregate_node_index=" + std::to_string(record.node_index)
          + " member_node_index=" + std::to_string(member_node_index_u32)
          + ")");
      }
      member_body_ids.push_back(*rigid_body);
    }

    std::ranges::sort(member_body_ids);
    member_body_ids.erase(
      std::ranges::unique(member_body_ids).begin(), member_body_ids.end());
    if (record.max_bodies > 0U
      && member_body_ids.size() > static_cast<size_t>(record.max_bodies)) {
      throw std::runtime_error(
        std::string("aggregate max_bodies exceeded by resolved rigid bodies ")
        + "(node_index=" + std::to_string(record.node_index)
        + " max_bodies=" + std::to_string(record.max_bodies)
        + " resolved_members=" + std::to_string(member_body_ids.size()) + ")");
    }

    const auto created = aggregate_api.CreateAggregate(world_id);
    if (!created.has_value()) {
      throw std::runtime_error(
        std::string("failed to create aggregate binding for node_index=")
        + std::to_string(record.node_index)
        + " reason=" + std::string(physics::to_string(created.error())));
    }

    const auto aggregate_id = created.value();
    auto keep_aggregate = false;
    ScopeGuard destroy_on_failure([&] noexcept -> void {
      if (!keep_aggregate) {
        (void)aggregate_api.DestroyAggregate(world_id, aggregate_id);
      }
    });

    for (const auto body_id : member_body_ids) {
      const auto add_result
        = aggregate_api.AddMemberBody(world_id, aggregate_id, body_id);
      if (!add_result.has_value()) {
        throw std::runtime_error(
          std::string("failed adding rigid body to aggregate ")
          + "(aggregate_node_index=" + std::to_string(record.node_index)
          + " body_id=" + physics::to_string(body_id) + " reason="
          + std::string(physics::to_string(add_result.error())) + ")");
      }
    }

    physics_module.RegisterNodeAggregateMapping(root_node.GetHandle(),
      aggregate_id, ToRuntimeAggregateAuthority(record.authority));
    if (!physics_module.HasAggregateForNode(root_node.GetHandle())
      || physics_module.GetAggregateIdForNode(root_node.GetHandle())
        != aggregate_id) {
      throw std::runtime_error(
        std::string("failed to register aggregate mapping for node_index=")
        + std::to_string(record.node_index));
    }

    keep_aggregate = true;
  }
}

void SceneLoaderService::HydratePhysicsBindings(
  const data::PhysicsSceneAsset& physics_asset)
{
  auto* physics_module = ResolvePhysicsModule().get();
  if (physics_module == nullptr) {
    throw std::runtime_error(
      "physics sidecar present but PhysicsModule is unavailable");
  }

  const auto rigid_body_bindings
    = physics_asset.GetBindings<data::pak::physics::RigidBodyBindingRecord>();
  const auto collider_bindings
    = physics_asset.GetBindings<data::pak::physics::ColliderBindingRecord>();
  const auto character_bindings
    = physics_asset.GetBindings<data::pak::physics::CharacterBindingRecord>();
  const auto soft_body_bindings
    = physics_asset.GetBindings<data::pak::physics::SoftBodyBindingRecord>();
  const auto joint_bindings
    = physics_asset.GetBindings<data::pak::physics::JointBindingRecord>();
  const auto vehicle_bindings
    = physics_asset.GetBindings<data::pak::physics::VehicleBindingRecord>();
  const auto vehicle_wheel_bindings
    = physics_asset
        .GetBindings<data::pak::physics::VehicleWheelBindingRecord>();
  const auto aggregate_bindings
    = physics_asset.GetBindings<data::pak::physics::AggregateBindingRecord>();

  const auto ccd_forbidden_node_indices
    = CollectVehicleAssociatedRigidBodyNodeIndices(
      vehicle_bindings, vehicle_wheel_bindings);
  // Shapes/materials are resolved per-binding via descriptor/resource lookups.
  // Record hydration order must remain dependency-safe and deterministic.
  HydrateRigidBodyBindings(
    *physics_module, rigid_body_bindings, ccd_forbidden_node_indices);
  HydrateColliderBindings(*physics_module, collider_bindings);
  HydrateCharacterBindings(*physics_module, character_bindings);
  HydrateSoftBodyBindings(*physics_module, soft_body_bindings);
  HydrateJointBindings(*physics_module, joint_bindings);
  HydrateVehicleBindings(
    *physics_module, vehicle_bindings, vehicle_wheel_bindings);
  HydrateAggregateBindings(
    *physics_module, aggregate_bindings, rigid_body_bindings);

  LOG_F(INFO,
    "SceneLoader: Physics hydration complete (rigid_bodies={} colliders={} "
    "characters={} soft_bodies={} joints={} vehicles={} aggregates={})",
    rigid_body_bindings.size(), collider_bindings.size(),
    character_bindings.size(), soft_body_bindings.size(), joint_bindings.size(),
    vehicle_bindings.size(), aggregate_bindings.size());
}

void SceneLoaderService::BeginHydrationWindow()
{
  if (runtime_scene_ == nullptr) {
    throw std::runtime_error(
      "physics hydration requires an instantiated runtime scene");
  }
  if (hydration_window_active_) {
    throw std::runtime_error(
      "physics hydration window re-entry is not supported");
  }
  hydration_window_active_ = true;
  hydration_transforms_resolved_ = false;
}

void SceneLoaderService::EndHydrationWindow() noexcept
{
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;
}

void SceneLoaderService::ResolveHydrationTransforms()
{
  if (!hydration_window_active_) {
    throw std::runtime_error(
      "ResolveHydrationTransforms requires an active hydration window");
  }
  if (hydration_transforms_resolved_) {
    return;
  }
  CHECK_NOTNULL_F(runtime_scene_.get());
  runtime_scene_->Update();
  hydration_transforms_resolved_ = true;
}

auto SceneLoaderService::ReadHydrationWorldPose(const size_t node_index) const
  -> std::pair<Vec3, Quat>
{
  if (!hydration_window_active_ || !hydration_transforms_resolved_) {
    throw std::runtime_error(
      "world transform read is forbidden before hydration transforms are "
      "resolved");
  }
  if (node_index >= runtime_nodes_.size()) {
    throw std::runtime_error(
      "hydration world-pose read node_index out of range");
  }

  const auto node = runtime_nodes_.at(node_index);
  const auto transform = node.GetTransform();
  const auto world_position = transform.GetWorldPosition();
  const auto world_rotation = transform.GetWorldRotation();
  if (!world_position.has_value() || !world_rotation.has_value()) {
    throw std::runtime_error(
      "world transform unavailable after ResolveHydrationTransforms");
  }

  return { *world_position, *world_rotation };
}

/*!
 Prime geometry dependencies while the scene asset is pending
 instantiation.
 This pins geometry assets by issuing load requests and keeping
 the loader references alive until `BuildSceneAsync()` completes. It prevents
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
  ready_geometry_keys_.clear();
}

//! Clear readiness tracking; owning scene/renderable pointers retain geometry.
void SceneLoaderService::ClearGeometryReadiness()
{
  // Scene bindings and runtime renderables own the geometry pointers; this
  // collection tracks readiness only.
  ready_geometry_keys_.clear();
  pending_geometry_keys_.clear();
}

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// The caller awaits at once and owns the referenced arguments.
auto SceneLoaderService::BuildSceneAsync(scene::Scene& scene,
  std::shared_ptr<const data::SceneAsset> asset) -> co::Co<scene::SceneNode>
// NOLINTEND(*-avoid-reference-coroutine-parameters)
{
  LOG_F(INFO, "SceneLoader: Instantiating runtime scene '{}'", kSceneName);
  scene.CollectMutationsStart();

  runtime_nodes_.clear();
  active_camera_ = {};
  runtime_scene_ = observer_ptr<scene::Scene> { &scene };
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;

  LogSceneSummary(*asset);
  const auto scripts = scripting::SceneScriptAttachment(
    loader_, *source_resolver_, compilation_service_);
  auto instance = co_await instantiation::Instantiate(
    instantiation::SceneInstantiationRequest {
      .asset = std::move(asset),
      .target = observer_ptr { &scene },
      .scripts = observer_ptr { &scripts },
      .input = input_system_,
    },
    loader_);
  runtime_nodes_ = std::move(instance.nodes);
  if (!instance.issues.empty()) {
    std::string problems;
    for (const auto& issue : instance.issues) {
      problems += problems.empty() ? "" : "; ";
      problems += issue.message;
    }
    LOG_F(WARNING, "SceneLoader: The scene loaded with {} problem{}: {}",
      instance.issues.size(), instance.issues.size() == 1 ? "" : "s", problems);
  }
  if (const auto active = scene.ActiveCamera(); active.has_value()) {
    active_camera_ = *active;
  }
  EnsureCameraAndViewport(scene);
  // Geometry pins are only needed until scene instantiation finishes.
  ClearGeometryReadiness();
  LogSceneHierarchy(scene);

  LOG_F(INFO, "SceneLoader: Runtime scene instantiation complete.");
  co_return std::move(active_camera_);
}

auto SceneLoaderService::FindHydrationPayload(
  const content::ResourceKey key) const noexcept
  -> observer_ptr<const data::PhysicsResource>
{
  const auto found = hydration_payloads_.find(key);
  return observer_ptr<const data::PhysicsResource> {
    found == hydration_payloads_.end() ? nullptr : found->second.get()
  };
}

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// The caller awaits at once and owns the referenced arguments.
auto SceneLoaderService::PreloadPhysicsDependencyResources(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<PhysicsPayloads>
// NOLINTEND(*-avoid-reference-coroutine-parameters)
{
  std::unordered_set<data::AssetKey> payload_asset_keys {};
  auto collect_payload_ref = [&](const data::AssetKey& shape_asset_key,
                               const std::string_view binding_kind,
                               const uint32_t node_index) -> void {
    const auto shape_desc
      = ResolveCollisionShapeAsset(shape_asset_key, binding_kind, node_index);
    if (!shape_desc.cooked_shape_ref.payload_asset_key.IsNil()) {
      payload_asset_keys.insert(shape_desc.cooked_shape_ref.payload_asset_key);
    }
  };
  auto collect_constraint_asset_key
    = [&](const data::AssetKey& asset_key) -> void {
    if (!asset_key.IsNil()) {
      payload_asset_keys.insert(asset_key);
    }
  };

  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::RigidBodyBindingRecord>()) {
    collect_payload_ref(
      record.shape_asset_key, "rigid-body", record.node_index);
  }
  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::CharacterBindingRecord>()) {
    collect_payload_ref(record.shape_asset_key, "character", record.node_index);
    if (!record.inner_shape_asset_key.IsNil()) {
      collect_payload_ref(record.inner_shape_asset_key, "character inner shape",
        record.node_index);
    }
  }
  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::ColliderBindingRecord>()) {
    collect_payload_ref(record.shape_asset_key, "collider", record.node_index);
  }
  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::JointBindingRecord>()) {
    collect_constraint_asset_key(record.constraint_asset_key);
  }
  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::VehicleBindingRecord>()) {
    collect_constraint_asset_key(record.constraint_asset_key);
  }
  for (const auto& record :
    physics_asset.GetBindings<data::pak::physics::SoftBodyBindingRecord>()) {
    collect_constraint_asset_key(record.topology_asset_key);
  }

  PhysicsPayloads owners;
  owners.reserve(payload_asset_keys.size());
  for (const auto& payload_asset_key : payload_asset_keys) {
    const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
      physics_asset, payload_asset_key);
    if (!resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string(
          "OXY-SHAPE-007: failed to resolve physics resource key for ")
        + data::to_string(payload_asset_key));
    }
    auto payload = co_await loader_.LoadPhysicsResourceAsync(*resource_key_opt);
    if (!payload) {
      throw std::runtime_error(
        std::string(
          "OXY-SHAPE-007: failed to preload physics payload resource ")
        + data::to_string(payload_asset_key));
    }
    owners.emplace(*resource_key_opt, std::move(payload));
  }
  co_return owners;
}

// NOLINTBEGIN(*-avoid-reference-coroutine-parameters)
// The caller awaits at once and owns the referenced arguments.
auto SceneLoaderService::HydratePhysicsSidecar(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<>
// NOLINTEND(*-avoid-reference-coroutine-parameters)
{
  BeginHydrationWindow();
  current_physics_context_ = observer_ptr { &physics_asset };
  ScopeGuard clear_context([this] noexcept -> void {
    hydration_payloads_.clear();
    EndHydrationWindow();
    current_physics_context_.reset();
  });
  ResolveHydrationTransforms();
  hydration_payloads_
    = co_await PreloadPhysicsDependencyResources(physics_asset);
  HydratePhysicsBindings(physics_asset);
  co_return;
}

void SceneLoaderService::LogSceneSummary(const data::SceneAsset& asset) const
{
  using data::pak::world::DirectionalLightRecord;
  using data::pak::world::LocalFogVolumeRecord;
  using data::pak::world::OrthographicCameraRecord;
  using data::pak::world::PerspectiveCameraRecord;
  using data::pak::world::PointLightRecord;
  using data::pak::world::RenderableRecord;
  using data::pak::world::SpotLightRecord;

  const auto nodes = asset.GetNodes();

  LOG_F(INFO,
    "SceneLoader: Scene summary: nodes={} renderables={} "
    "perspective_cameras={} orthographic_cameras={} "
    "directional_lights={} point_lights={} spot_lights={} "
    "local_fog_volumes={}",
    nodes.size(), asset.GetComponents<RenderableRecord>().size(),
    asset.GetComponents<PerspectiveCameraRecord>().size(),
    asset.GetComponents<OrthographicCameraRecord>().size(),
    asset.GetComponents<DirectionalLightRecord>().size(),
    asset.GetComponents<PointLightRecord>().size(),
    asset.GetComponents<SpotLightRecord>().size(),
    asset.GetComponents<LocalFogVolumeRecord>().size());
}

void SceneLoaderService::EnsureCameraAndViewport(scene::Scene& scene)
{
  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(extent_.width),
    .height = static_cast<float>(extent_.height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };

  if (!active_camera_.IsAlive()) {
    active_camera_ = scene.CreateNode("MainCamera");
    constexpr glm::vec3 cam_pos(10.0F, 10.0F, 10.0F);
    constexpr glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));
    const auto handle = active_camera_.GetHandle();
    const bool already_tracked = std::ranges::any_of(
      runtime_nodes_, [&](const scene::SceneNode& node) -> bool {
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
  // The demo views through its fallback camera like an authored one.
  (void)scene.SetActiveCamera(active_camera_);

  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    auto& cam = cam_ref->get();
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
  const auto PrintNodeLine
    = [](scene::SceneNode& node, const int depth) -> void {
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

  // Instanced scenes add nodes beyond the scene's own, so compare against
  // every node of the runtime scene: a gap means nodes hang off no root.
  if (visited_nodes.size() != scene.GetNodeCount()) {
    LOG_F(WARNING, "SceneLoader: Hierarchy traversal visited {} of {} nodes.",
      visited_nodes.size(), scene.GetNodeCount());
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
