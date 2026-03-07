//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Character/CharacterController.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/SoftBody/SoftBodyDesc.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>
#include <Oxygen/PhysicsModule/PhysicsModule.h>
#include <Oxygen/PhysicsModule/ScenePhysics.h>
#include <Oxygen/Platform/Input.h>
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
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

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

  auto IsNearlyEqual(const float lhs, const float rhs) noexcept -> bool
  {
    return std::abs(lhs - rhs) <= 1e-5F;
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

    auto current = runtime_nodes[candidate_index];
    const auto ancestor_handle = runtime_nodes[ancestor_index].GetHandle();
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

  auto ScriptParamFromRecord(
    const data::pak::scripting::ScriptParamRecord& record)
    -> std::optional<data::ScriptParam>
  {
    using data::pak::scripting::ScriptParamType;
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

  auto ToActionValueType(const uint8_t value_type_id)
    -> std::optional<input::ActionValueType>
  {
    switch (value_type_id) {
    case static_cast<uint8_t>(input::ActionValueType::kBool):
      return input::ActionValueType::kBool;
    case static_cast<uint8_t>(input::ActionValueType::kAxis1D):
      return input::ActionValueType::kAxis1D;
    case static_cast<uint8_t>(input::ActionValueType::kAxis2D):
      return input::ActionValueType::kAxis2D;
    default:
      return std::nullopt;
    }
  }

  auto ResolveInputSlot(const std::string_view slot_name)
    -> const platform::InputSlot*
  {
    if (slot_name.empty()) {
      return nullptr;
    }

    if (slot_name == "Keyboard.PgUp" || slot_name == "Keyboard.PageUp") {
      return &platform::InputSlots::PageUp;
    }
    if (slot_name == "Keyboard.PgDn" || slot_name == "Keyboard.PageDown") {
      return &platform::InputSlots::PageDown;
    }
    if (slot_name == "Keyboard.End") {
      return &platform::InputSlots::End;
    }

    static const std::vector<platform::InputSlot> all_slots = [] {
      std::vector<platform::InputSlot> slots;
      platform::InputSlots::GetAllInputSlots(slots);
      return slots;
    }();
    for (const auto& slot : all_slots) {
      const auto name = slot.GetName();
      if (slot_name == name) {
        return &slot;
      }

      std::string keyboard_name("Keyboard.");
      keyboard_name += name;
      if (slot_name == keyboard_name) {
        return &slot;
      }
    }

    return nullptr;
  }

  void ApplyTriggerBehavior(
    const data::pak::input::InputTriggerBehavior behavior,
    const std::shared_ptr<input::ActionTrigger>& trigger)
  {
    if (!trigger) {
      return;
    }
    switch (behavior) {
    case data::pak::input::InputTriggerBehavior::kExplicit:
      trigger->MakeExplicit();
      break;
    case data::pak::input::InputTriggerBehavior::kBlocker:
      trigger->MakeBlocker();
      break;
    case data::pak::input::InputTriggerBehavior::kImplicit:
    default:
      trigger->MakeImplicit();
      break;
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

  auto CompilerFingerprintForLanguage(
    const data::pak::scripting::ScriptLanguage language) -> uint64_t
  {
    switch (language) {
    case data::pak::scripting::ScriptLanguage::kLuau:
      return kLuauCompilerFingerprint;
    default:
      return kUnknownCompilerFingerprint;
    }
  }

  auto VmBytecodeVersionForLanguage(
    const data::pak::scripting::ScriptLanguage language) -> uint64_t
  {
    switch (language) {
    case data::pak::scripting::ScriptLanguage::kLuau:
      return kLuauVmBytecodeVersion;
    default:
      return kUnknownVmBytecodeVersion;
    }
  }

  auto ComputeCompileKey(const data::AssetKey asset_key,
    const scripting::ScriptSourceBlob& blob,
    const core::meta::scripting::ScriptCompileMode compile_mode)
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

  auto AssetNameView(const data::pak::core::AssetHeader& header)
    -> std::string_view
  {
    const auto* const begin = std::begin(header.name);
    const auto* const end = std::end(header.name);
    const auto* const nul = std::ranges::find(header.name, '\0');
    return { begin, static_cast<size_t>((nul == end ? end : nul) - begin) };
  }

  auto JoinScriptRoots(const std::vector<std::filesystem::path>& roots)
    -> std::string
  {
    if (roots.empty()) {
      return "<none>";
    }
    std::string out;
    for (size_t i = 0; i < roots.size(); ++i) {
      if (i > 0) {
        out.append("; ");
      }
      out.append(roots[i].lexically_normal().generic_string());
    }
    return out;
  }

  auto NormalizeExternalProbePath(std::string_view path_text)
    -> std::filesystem::path
  {
    std::filesystem::path path { path_text };
    if (path.extension().empty()) {
      path.replace_extension(".luau");
    }
    return path.lexically_normal();
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
  , compilation_service_(std::move(compilation_service))
  , source_resolver_(
      std::make_unique<scripting::ScriptSourceResolver>(path_finder_))
{
  if (engine_) {
    physics_module_subscription_ = engine_->SubscribeModuleAttached(
      [this](const engine::ModuleEvent& event) { OnModuleAttached(event); },
      /*replay_existing=*/true);
  }
}

SceneLoaderService::~SceneLoaderService()
{
  physics_module_subscription_.Cancel();
  // Ensure any geometry pins are released if the loader is torn down early.
  ReleasePinnedGeometryAssets();
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
  swap_.physics_asset.reset();
  runtime_scene_ = observer_ptr<scene::Scene> { nullptr };
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;
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

    LOG_F(INFO, "SceneLoader: Scene asset loaded. Validating physics sidecar.");

    runtime_nodes_.clear();
    active_camera_ = {};
    pending_geometry_keys_.clear();
    pinned_geometry_keys_.clear();

    const auto scene_asset = std::move(asset);
    const auto sidecar_key_opt
      = ResolvePhysicsSidecarKey(scene_asset->GetAssetKey());

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
      [weak_self = weak_from_this(), scene_asset, sidecar_key](
        std::shared_ptr<data::PhysicsSceneAsset> physics_asset) {
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

auto SceneLoaderService::ResolvePhysicsSidecarKey(
  const data::AssetKey& scene_key) const -> std::optional<data::AssetKey>
{
  return loader_.FindPhysicsSidecarAssetKeyForScene(scene_key);
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
    > static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
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
  if (!current_physics_context_asset_key_.has_value()) {
    throw std::runtime_error(std::string(binding_kind)
      + " shape_asset_key resolution requires active physics hydration "
        "context (node_index="
      + std::to_string(node_index)
      + " shape_asset_key=" + data::to_string(shape_asset_key) + ")");
  }
  const auto shape_desc_opt = loader_.ReadCollisionShapeAssetDescForAsset(
    *current_physics_context_asset_key_, shape_asset_key);
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
  if (!current_physics_context_asset_key_.has_value()) {
    throw std::runtime_error(std::string(binding_kind)
      + " material_asset_key resolution requires active physics hydration "
        "context (node_index="
      + std::to_string(node_index)
      + " material_asset_key=" + data::to_string(material_asset_key) + ")");
  }
  const auto material_desc_opt = loader_.ReadPhysicsMaterialAssetDescForAsset(
    *current_physics_context_asset_key_, material_asset_key);
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
  const auto ensure_no_cooked_ref = [&]() {
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
    if (!(radius > 0.0F && half_height > 0.0F)) {
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
    if (!(half_extents[0] > 0.0F && half_extents[1] > 0.0F
          && half_extents[2] > 0.0F)) {
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
    if (!(radius > 0.0F && half_height > 0.0F)) {
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
    if (!(radius > 0.0F && half_height > 0.0F)) {
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
        data::pak::physics::ShapePayloadType::kConvex, binding_kind, node_index)
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
    if (!(params.limits_max[0] > params.limits_min[0]
          && params.limits_max[1] > params.limits_min[1]
          && params.limits_max[2] > params.limits_min[2])) {
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
    return physics::CompoundShape { .cooked_payload
      = ResolveCookedShapePayload(shape_desc,
        data::pak::physics::ShapePayloadType::kCompound, binding_kind,
        node_index) };
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
  if (!current_physics_context_asset_key_.has_value()) {
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
    *current_physics_context_asset_key_, cooked_ref.payload_asset_key);
  if (!resource_key_opt.has_value()) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: failed to resolve physics resource key in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " payload_asset_key="
      + data::to_string(cooked_ref.payload_asset_key) + ")");
  }

  auto resource = loader_.GetPhysicsResource(*resource_key_opt);
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
  if (!current_physics_context_asset_key_.has_value()) {
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
        *current_physics_context_asset_key_, record.constraint_asset_key);
      if (!resource_key_opt.has_value()) {
        throw std::runtime_error(
          std::string("joint constraint_asset_key could not be resolved ")
          + "(node_index_a=" + std::to_string(record.node_index_a)
          + " node_index_b=" + std::to_string(record.node_index_b)
          + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
      }
      const auto constraint_resource
        = loader_.GetPhysicsResource(*resource_key_opt);
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
      runtime_nodes_[node_index_a].GetHandle());
    if (!body_a.has_value()) {
      throw std::runtime_error(
        std::string("joint node_index_a does not have a rigid body: ")
        + std::to_string(record.node_index_a));
    }
    auto body_b = physics::ScenePhysics::GetRigidBody(
      observer_ptr<physics::PhysicsModule> { &physics_module },
      runtime_nodes_[node_index_b].GetHandle());
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
    auto& node = runtime_nodes_[node_index];
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
          desc.mass_kg = (std::max)(0.001F, material_desc.density);
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
    auto& node = runtime_nodes_[node_index];
    const auto attached = physics::ScenePhysics::AttachRigidBodyDetailed(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      const auto reason_text
        = std::string(physics::to_string(attached.error()));
      const auto format_quat = [](const Quat& q) {
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
    auto& node = runtime_nodes_[node_index];
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
  if (!current_physics_context_asset_key_.has_value()) {
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
    const auto expected_settings_format_name = use_jolt_settings
      ? "jolt_soft_body_shared_settings_binary"
      : "physx_soft_body_settings_binary";
    const auto selected_backend_name = use_jolt_settings ? "jolt" : "physx";
    if (record.topology_format != expected_settings_format) {
      throw std::runtime_error(
        std::string("soft-body topology_format does not match active backend ")
        + "(node_index=" + std::to_string(record.node_index)
        + " topology_format="
        + std::to_string(static_cast<uint32_t>(record.topology_format))
        + " backend=" + selected_backend_name + ")");
    }

    const auto validate_non_negative_finite
      = [&](const float value, const std::string_view field) {
          if (!std::isfinite(value) || value < 0.0F) {
            throw std::runtime_error(std::string("soft-body ")
              + std::string(field) + " must be finite and >= 0 (node_index="
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
        *current_physics_context_asset_key_, selected_settings_asset_key);
    if (!settings_resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string("soft-body selected settings resource key could not be "
                    "resolved ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(selected_settings_asset_key)
        + " backend=" + selected_backend_name + ")");
    }
    const auto settings_resource
      = loader_.GetPhysicsResource(*settings_resource_key_opt);
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
    auto& node = runtime_nodes_[node_index];
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
  if (!current_physics_context_asset_key_.has_value()) {
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
      *current_physics_context_asset_key_, record.constraint_asset_key);
    if (!resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string("vehicle constraint_asset_key could not be resolved ")
        + "(node_index=" + std::to_string(record.node_index)
        + " asset_key=" + data::to_string(record.constraint_asset_key) + ")");
    }
    const auto constraint_resource
      = loader_.GetPhysicsResource(*resource_key_opt);
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
      runtime_nodes_[chassis_node_index].GetHandle());
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
      const auto& wheel_binding = wheel_bindings[wheel_slice_offset + i];
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
        runtime_nodes_[wheel_node_index].GetHandle());
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
      = runtime_nodes_[chassis_node_index].GetHandle();
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

    auto& root_node = runtime_nodes_[root_node_index];
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
        runtime_nodes_[member_node_index].GetHandle());
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
    ScopeGuard destroy_on_failure([&]() noexcept {
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

  const auto node = runtime_nodes_[node_index];
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
  scene.CollectMutationsStart();

  runtime_nodes_.clear();
  active_camera_ = {};
  runtime_scene_ = observer_ptr<scene::Scene> { &scene };
  hydration_window_active_ = false;
  hydration_transforms_resolved_ = false;

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

auto SceneLoaderService::PreloadPhysicsDependencyResources(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<>
{
  const auto context_asset_key = physics_asset.GetAssetKey();

  std::unordered_set<data::AssetKey> payload_asset_keys {};
  auto collect_payload_ref = [&](const data::AssetKey& shape_asset_key,
                               const std::string_view binding_kind,
                               const uint32_t node_index) {
    const auto shape_desc
      = ResolveCollisionShapeAsset(shape_asset_key, binding_kind, node_index);
    if (!shape_desc.cooked_shape_ref.payload_asset_key.IsNil()) {
      payload_asset_keys.insert(shape_desc.cooked_shape_ref.payload_asset_key);
    }
  };
  auto collect_constraint_asset_key = [&](const data::AssetKey& asset_key) {
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

  for (const auto& payload_asset_key : payload_asset_keys) {
    const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
      context_asset_key, payload_asset_key);
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
  }
}

auto SceneLoaderService::HydratePhysicsSidecar(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<>
{
  current_physics_context_asset_key_ = physics_asset.GetAssetKey();
  BeginHydrationWindow();
  ScopeGuard clear_context([this]() noexcept {
    EndHydrationWindow();
    current_physics_context_asset_key_.reset();
  });
  ResolveHydrationTransforms();
  co_await PreloadPhysicsDependencyResources(physics_asset);
  HydratePhysicsBindings(physics_asset);
  co_return;
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
  using data::pak::world::DirectionalLightRecord;
  using data::pak::world::OrthographicCameraRecord;
  using data::pak::world::PerspectiveCameraRecord;
  using data::pak::world::PointLightRecord;
  using data::pak::world::RenderableRecord;
  using data::pak::world::SpotLightRecord;

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
  using data::pak::world::NodeRecord;

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
  using data::pak::world::RenderableRecord;

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
  using data::pak::world::DirectionalLightRecord;
  using data::pak::world::PointLightRecord;
  using data::pak::world::SpotLightRecord;

  const auto ApplyCommonLight =
    [](scene::CommonLightProperties& dst,
      const data::pak::world::LightCommonRecord& src) {
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
  using data::pak::scripting::ScriptingComponentRecord;

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
    auto slot_records = loader_.GetHydratedScriptSlots(asset, component);
    LOG_F(INFO, "hydrated script slots (node_index={}, slots={})", node_index,
      slot_records.size());

    for (const auto& slot_record : slot_records) {
      if (slot_record.script_asset_key == data::AssetKey {}) {
        LOG_F(WARNING,
          "skipping hydrated script slot with empty script asset key "
          "(node_index={})",
          node_index);
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
  std::span<const data::pak::scripting::ScriptParamRecord> params)
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
    = [this, context_asset_key = script_asset->GetAssetKey()](
        const uint32_t index) -> std::shared_ptr<const data::ScriptResource> {
    return ReadScriptResource(index, context_asset_key);
  };

  auto map_origin
    = [this, context_asset_key = script_asset->GetAssetKey()](
        const uint32_t index) -> std::optional<scripting::ScriptBlobOrigin> {
    if (auto res = ReadScriptResource(index, context_asset_key)) {
      return scripting::ScriptBlobOrigin::kEmbeddedResource;
    }
    return std::nullopt;
  };

  auto resolve_result = source_resolver_->Resolve(
    scripting::IScriptSourceResolver::ResolveRequest {
      .asset = *script_asset,
      .load_script_resource = std::move(load_script_resource),
      .map_resource_origin = std::move(map_origin),
    });
  if (!resolve_result.ok) {
    const auto external_path = script_asset->TryGetExternalSourcePath();
    const auto script_roots = path_finder_.ScriptSourceRoots();
    LOG_F(ERROR,
      "failed to resolve script source: {} (asset_key={}, script_name='{}', "
      "allows_external={}, has_embedded={}, bytecode_index={}, "
      "source_index={}, external_source_path='{}')",
      resolve_result.error_message,
      data::to_string(script_asset->GetAssetKey()),
      AssetNameView(script_asset->GetHeader()),
      script_asset->AllowsExternalSource() ? "true" : "false",
      script_asset->HasEmbeddedResource() ? "true" : "false",
      script_asset->GetBytecodeResourceIndex(),
      script_asset->GetSourceResourceIndex(),
      external_path.has_value() ? std::string(*external_path) : "<none>");
    LOG_F(ERROR,
      "script source resolve context (asset_key={}, workspace_root='{}', "
      "source_pak='{}', script_roots={})",
      data::to_string(script_asset->GetAssetKey()),
      path_finder_.WorkspaceRoot().lexically_normal().generic_string(),
      source_pak_path_.lexically_normal().generic_string(),
      JoinScriptRoots(script_roots));
    if (external_path.has_value()) {
      const auto external_relative = NormalizeExternalProbePath(*external_path);
      for (const auto& root : script_roots) {
        const auto candidate = (root / external_relative).lexically_normal();
        const auto exists = std::filesystem::exists(candidate);
        LOG_F(ERROR,
          "script source resolve probe (asset_key={}, root='{}', "
          "relative='{}', candidate='{}', exists={})",
          data::to_string(script_asset->GetAssetKey()),
          root.lexically_normal().generic_string(),
          external_relative.generic_string(), candidate.generic_string(),
          exists ? "true" : "false");
      }
    } else {
      LOG_F(ERROR,
        "script source resolve probe skipped (asset_key={}): no external "
        "source path is set",
        data::to_string(script_asset->GetAssetKey()));
    }
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
  constexpr auto kCompileMode
    = core::meta::scripting::ScriptCompileMode::kDebug;
  const auto compile_key
    = ComputeCompileKey(script_asset->GetAssetKey(), source_blob, kCompileMode);
  const auto source_size = source_blob.Size();
  const auto source_hash = source_blob.ContentHash();
  LOG_F(INFO,
    "submitting compile request (asset_key={}, compile_key={}, source_size={}, "
    "source_hash={})",
    data::to_string(script_asset->GetAssetKey()), compile_key, source_size,
    source_hash);
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
  LOG_F(INFO,
    "queued script compilation (asset_key={}, compile_key={}, source_hash={})",
    data::to_string(script_asset->GetAssetKey()), compile_key, source_hash);
}

void SceneLoaderService::SelectActiveCamera(const data::SceneAsset& asset)
{
  using data::pak::world::OrthographicCameraRecord;
  using data::pak::world::PerspectiveCameraRecord;

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

auto SceneLoaderService::ReadScriptResource(
  uint32_t index, const data::AssetKey& context_asset_key) const
  -> std::shared_ptr<const data::ScriptResource>
{
  const auto resource_key_opt = loader_.MakeScriptResourceKeyForAsset(
    context_asset_key, data::pak::core::ResourceIndexT { index });
  if (!resource_key_opt.has_value()) {
    LOG_F(WARNING,
      "failed to resolve script resource key (context_asset={} index={})",
      data::to_string(context_asset_key), index);
    return nullptr;
  }

  auto resource = loader_.GetScriptResource(*resource_key_opt);
  if (!resource) {
    resource = std::const_pointer_cast<data::ScriptResource>(
      loader_.ReadScriptResourceForAsset(
        context_asset_key, data::pak::core::ResourceIndexT { index }));
  }
  if (!resource) {
    LOG_F(WARNING,
      "script resource unavailable (context_asset={} index={} key={:#x})",
      data::to_string(context_asset_key), index, resource_key_opt->get());
  }
  return resource;
}

} // namespace oxygen::examples
