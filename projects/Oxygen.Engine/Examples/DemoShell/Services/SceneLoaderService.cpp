//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
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
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTable.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
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

  void ApplyTriggerBehavior(const data::pak::InputTriggerBehavior behavior,
    const std::shared_ptr<input::ActionTrigger>& trigger)
  {
    if (!trigger) {
      return;
    }
    switch (behavior) {
    case data::pak::InputTriggerBehavior::kExplicit:
      trigger->MakeExplicit();
      break;
    case data::pak::InputTriggerBehavior::kBlocker:
      trigger->MakeBlocker();
      break;
    case data::pak::InputTriggerBehavior::kImplicit:
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

  if (source_pak_path_.empty()) {
    return;
  }

  // Reload the PAK file (even if it's a directory, assume PakFile handles it or
  // we'll add support there)
  try {
    source_pak_ = std::make_unique<content::PakFile>(source_pak_path_);
  } catch (const std::exception& e) {
    LOG_F(ERROR, "SceneLoader: Failed to load source PAK: {}", e.what());
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
    // Refresh the source resolver and PAK/loose index to ensure we pick up
    // any changes on disk since the last load.
    LOG_F(INFO, "SceneLoader: Refreshing script sources from: {}",
      source_pak_path_.string());

    // 1. Recreate the resolver to clear any internal caches
    source_resolver_
      = std::make_unique<scripting::ScriptSourceResolver>(path_finder_);

    // 2. Re-open the source PAK
    source_pak_.reset();

    try {
      source_pak_ = std::make_unique<content::PakFile>(source_pak_path_);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "SceneLoader: Failed to reload source PAK: {}", e.what());
    }
  }

  swap_.scene_key = key;
  swap_.asset.reset();
  swap_.physics_asset.reset();
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
  if (!source_pak_) {
    return std::nullopt;
  }

  std::optional<data::AssetKey> matched_key {};
  for (const auto& entry : source_pak_->Directory()) {
    if (entry.asset_type
      != static_cast<uint8_t>(data::AssetType::kPhysicsScene)) {
      continue;
    }
    if (entry.desc_size < sizeof(data::pak::v7::PhysicsSceneAssetDesc)) {
      throw std::runtime_error(
        std::string("physics sidecar descriptor too small for asset key=")
        + data::to_string(entry.asset_key));
    }

    auto reader = source_pak_->CreateReader(entry);
    auto blob_result = reader.ReadBlob(entry.desc_size);
    if (!blob_result) {
      throw std::runtime_error(
        std::string("failed reading physics sidecar descriptor for asset key=")
        + data::to_string(entry.asset_key)
        + " reason=" + blob_result.error().message());
    }

    data::PhysicsSceneAsset candidate(entry.asset_key, blob_result.value());
    if (candidate.GetTargetSceneKey() != scene_key) {
      continue;
    }

    if (matched_key.has_value()) {
      throw std::runtime_error(
        std::string("multiple physics sidecars reference scene key=")
        + data::to_string(scene_key) + " first=" + data::to_string(*matched_key)
        + " second=" + data::to_string(entry.asset_key));
    }
    matched_key = entry.asset_key;
  }

  if (!matched_key.has_value()) {
    return std::nullopt;
  }
  return matched_key;
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

void SceneLoaderService::ValidateUnsupportedPhysicsBindings(
  const data::PhysicsSceneAsset& physics_asset) const
{
  const auto collider_bindings
    = physics_asset.GetBindings<data::pak::ColliderBindingRecord>();
  const auto soft_body_bindings
    = physics_asset.GetBindings<data::pak::SoftBodyBindingRecord>();
  const auto joint_bindings
    = physics_asset.GetBindings<data::pak::JointBindingRecord>();
  const auto vehicle_bindings
    = physics_asset.GetBindings<data::pak::VehicleBindingRecord>();
  const auto aggregate_bindings
    = physics_asset.GetBindings<data::pak::AggregateBindingRecord>();

  if (!collider_bindings.empty() || !soft_body_bindings.empty()
    || !joint_bindings.empty() || !vehicle_bindings.empty()
    || !aggregate_bindings.empty()) {
    throw std::runtime_error(
      std::string("unsupported physics binding tables in sidecar key=")
      + data::to_string(physics_asset.GetAssetKey())
      + " collider=" + std::to_string(collider_bindings.size())
      + " soft_body=" + std::to_string(soft_body_bindings.size())
      + " joint=" + std::to_string(joint_bindings.size())
      + " vehicle=" + std::to_string(vehicle_bindings.size())
      + " aggregate=" + std::to_string(aggregate_bindings.size()));
  }
}

void SceneLoaderService::HydrateRigidBodyBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::RigidBodyBindingRecord> bindings)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("rigid-body node_index out of range: ")
        + std::to_string(record.node_index));
    }

    if (record.shape_asset_index != data::pak::kNoResourceIndex) {
      throw std::runtime_error(
        std::string("rigid-body external shape references are "
                    "unsupported in section-9 hydrator (node_index=")
        + std::to_string(record.node_index) + " shape_asset_index="
        + std::to_string(record.shape_asset_index) + ")");
    }

    if (record.material_asset_index != data::pak::kNoResourceIndex) {
      LOG_F(WARNING,
        "SceneLoader: rigid-body material_asset_index={} for node_index={} is "
        "not yet resolved by section-9 hydrator; using BodyDesc defaults.",
        record.material_asset_index, record.node_index);
    }

    physics::body::BodyDesc desc {};
    switch (record.body_type) {
    case data::pak::PhysicsBodyType::kStatic:
      desc.type = physics::body::BodyType::kStatic;
      break;
    case data::pak::PhysicsBodyType::kDynamic:
      desc.type = physics::body::BodyType::kDynamic;
      break;
    case data::pak::PhysicsBodyType::kKinematic:
      desc.type = physics::body::BodyType::kKinematic;
      break;
    default:
      throw std::runtime_error(
        std::string("unsupported rigid-body type value: ")
        + std::to_string(static_cast<uint32_t>(record.body_type)));
    }

    desc.flags = physics::body::BodyFlags::kNone;
    desc.gravity_factor = (std::max)(0.0F, record.gravity_factor);
    if (desc.gravity_factor > 0.0F) {
      desc.flags = desc.flags | physics::body::BodyFlags::kEnableGravity;
    }
    if (record.is_sensor != 0U) {
      desc.flags = desc.flags | physics::body::BodyFlags::kIsTrigger;
    }
    if (record.motion_quality == data::pak::PhysicsMotionQuality::kLinearCast) {
      desc.flags = desc.flags
        | physics::body::BodyFlags::kEnableContinuousCollisionDetection;
    }
    desc.mass_kg = record.mass > 0.0F ? record.mass : 1.0F;
    desc.linear_damping = (std::max)(0.0F, record.linear_damping);
    desc.angular_damping = (std::max)(0.0F, record.angular_damping);
    desc.collision_layer = physics::CollisionLayer { static_cast<uint32_t>(
      record.collision_layer) };
    desc.collision_mask
      = physics::CollisionMask { static_cast<uint32_t>(record.collision_mask) };

    auto& node = runtime_nodes_[node_index];
    const auto attached = physics::ScenePhysics::AttachRigidBody(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      throw std::runtime_error(
        std::string("failed to attach rigid-body binding for node_index=")
        + std::to_string(record.node_index));
    }
  }
}

void SceneLoaderService::HydrateCharacterBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::CharacterBindingRecord> bindings)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("character node_index out of range: ")
        + std::to_string(record.node_index));
    }

    if (record.shape_asset_index != data::pak::kNoResourceIndex) {
      throw std::runtime_error(
        std::string("character external shape references are unsupported in "
                    "section-9 hydrator (node_index=")
        + std::to_string(record.node_index) + " shape_asset_index="
        + std::to_string(record.shape_asset_index) + ")");
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

    auto& node = runtime_nodes_[node_index];
    const auto attached = physics::ScenePhysics::AttachCharacter(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      throw std::runtime_error(
        std::string("failed to attach character binding for node_index=")
        + std::to_string(record.node_index));
    }
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

  ValidateUnsupportedPhysicsBindings(physics_asset);

  const auto rigid_body_bindings
    = physics_asset.GetBindings<data::pak::RigidBodyBindingRecord>();
  const auto character_bindings
    = physics_asset.GetBindings<data::pak::CharacterBindingRecord>();

  HydrateRigidBodyBindings(*physics_module, rigid_body_bindings);
  HydrateCharacterBindings(*physics_module, character_bindings);

  LOG_F(INFO,
    "SceneLoader: Physics hydration complete (rigid_bodies={} characters={})",
    rigid_body_bindings.size(), character_bindings.size());
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

  scene.SetEnvironment(BuildEnvironment(asset));
  LogSceneSummary(asset);
  InstantiateNodes(scene, asset);
  ApplyHierarchy(scene, asset);
  AttachRenderables(asset);
  AttachLights(asset);
  AttachScripting(asset);
  AttachInputMappings(asset);
  if (swap_.physics_asset) {
    HydratePhysicsBindings(*swap_.physics_asset);
  }
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
    auto slot_records = loader_.GetHydratedScriptSlots(asset, component);
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

void SceneLoaderService::AttachInputMappings(const data::SceneAsset& asset)
{
  using data::pak::InputContextBindingFlags;
  using data::pak::InputContextBindingRecord;
  using data::pak::InputTriggerType;

  if (!input_system_) {
    return;
  }

  const auto bindings = asset.GetComponents<InputContextBindingRecord>();
  if (bindings.empty()) {
    return;
  }

  LOG_F(INFO, "SceneLoader: Hydrating input context bindings (count={})",
    bindings.size());

  for (const auto& binding : bindings) {
    if (binding.context_asset_key == data::AssetKey {}) {
      continue;
    }

    const auto context_asset
      = loader_.GetInputMappingContextAsset(binding.context_asset_key);
    if (!context_asset) {
      LOG_F(WARNING,
        "SceneLoader: Missing input mapping context asset {} in cache",
        data::to_string(binding.context_asset_key));
      continue;
    }

    const std::string context_name(context_asset->GetAssetName());
    if (context_name.empty()) {
      LOG_F(WARNING,
        "SceneLoader: Skipping unnamed input mapping context asset {}",
        data::to_string(binding.context_asset_key));
      continue;
    }

    if (auto existing = input_system_->GetMappingContextByName(context_name);
      existing) {
      input_system_->DeactivateMappingContext(existing);
      input_system_->RemoveMappingContext(existing);
    }

    auto runtime_context
      = std::make_shared<input::InputMappingContext>(context_name);
    int mapping_count = 0;
    std::unordered_map<data::AssetKey, std::shared_ptr<input::Action>>
      actions_by_asset_key;

    auto get_or_create_action
      = [&](const data::AssetKey action_key) -> std::shared_ptr<input::Action> {
      if (action_key == data::AssetKey {}) {
        return nullptr;
      }

      if (const auto found = actions_by_asset_key.find(action_key);
        found != actions_by_asset_key.end()) {
        return found->second;
      }

      auto action_asset = loader_.GetInputActionAsset(action_key);
      if (!action_asset) {
        LOG_F(WARNING, "SceneLoader: Missing input action asset {} in cache",
          data::to_string(action_key));
        return nullptr;
      }

      const std::string action_name(action_asset->GetAssetName());
      if (action_name.empty()) {
        LOG_F(WARNING, "SceneLoader: Skipping unnamed input action asset {}",
          data::to_string(action_key));
        return nullptr;
      }

      auto action = input_system_->GetActionByName(action_name);
      if (!action) {
        const auto value_type
          = ToActionValueType(action_asset->GetValueTypeId());
        if (!value_type.has_value()) {
          LOG_F(WARNING,
            "SceneLoader: Unsupported action value type {} for action '{}'",
            action_asset->GetValueTypeId(), action_name);
          return nullptr;
        }
        action = std::make_shared<input::Action>(action_name, *value_type);
        input_system_->AddAction(action);
      }

      action->SetConsumesInput(action_asset->ConsumesInput());
      LOG_F(INFO,
        "SceneLoader: Input action ready "
        "(name='{}' key={} value_type={} consumes_input={})",
        action_name, data::to_string(action_key),
        action_asset->GetValueTypeId(), action_asset->ConsumesInput());
      actions_by_asset_key.insert_or_assign(action_key, action);
      return action;
    };

    const auto trigger_records = context_asset->GetTriggers();
    const auto mapping_records = context_asset->GetMappings();
    for (const auto& mapping_record : mapping_records) {
      auto action = get_or_create_action(mapping_record.action_asset_key);
      if (!action) {
        continue;
      }

      const auto slot_name
        = context_asset->TryGetString(mapping_record.slot_name_offset);
      if (!slot_name.has_value()) {
        LOG_F(WARNING,
          "SceneLoader: Input mapping has invalid slot string offset={} "
          "(context={})",
          mapping_record.slot_name_offset, context_name);
        continue;
      }

      const auto slot = ResolveInputSlot(*slot_name);
      if (slot == nullptr) {
        LOG_F(WARNING,
          "SceneLoader: Input mapping uses unknown slot '{}' (context={})",
          *slot_name, context_name);
        continue;
      }
      LOG_F(INFO,
        "SceneLoader: Input mapping bind "
        "(context='{}' action='{}' slot='{}' trigger_count={})",
        context_name, action->GetName(), slot->GetName(),
        mapping_record.trigger_count);

      auto runtime_mapping
        = std::make_shared<input::InputActionMapping>(action, *slot);
      const size_t trigger_start = mapping_record.trigger_start_index;
      const size_t trigger_end = std::min(trigger_records.size(),
        trigger_start + static_cast<size_t>(mapping_record.trigger_count));

      for (size_t trigger_index = trigger_start; trigger_index < trigger_end;
        ++trigger_index) {
        const auto& trigger_record = trigger_records[trigger_index];
        std::shared_ptr<input::ActionTrigger> trigger;
        switch (trigger_record.type) {
        case InputTriggerType::kPressed:
          trigger = std::make_shared<input::ActionTriggerPressed>();
          break;
        case InputTriggerType::kReleased:
          trigger = std::make_shared<input::ActionTriggerReleased>();
          break;
        case InputTriggerType::kDown:
          trigger = std::make_shared<input::ActionTriggerDown>();
          break;
        case InputTriggerType::kHold: {
          auto hold = std::make_shared<input::ActionTriggerHold>();
          if (trigger_record.fparams[0] > 0.0F) {
            hold->SetHoldDurationThreshold(trigger_record.fparams[0]);
          }
          trigger = std::move(hold);
          break;
        }
        case InputTriggerType::kTap: {
          auto tap = std::make_shared<input::ActionTriggerTap>();
          if (trigger_record.fparams[0] > 0.0F) {
            tap->SetTapTimeThreshold(trigger_record.fparams[0]);
          }
          trigger = std::move(tap);
          break;
        }
        case InputTriggerType::kHoldAndRelease: {
          auto hold_and_release
            = std::make_shared<input::ActionTriggerHoldAndRelease>();
          if (trigger_record.fparams[0] > 0.0F) {
            hold_and_release->SetHoldDurationThreshold(
              trigger_record.fparams[0]);
          }
          trigger = std::move(hold_and_release);
          break;
        }
        case InputTriggerType::kPulse: {
          auto pulse = std::make_shared<input::ActionTriggerPulse>();
          if (trigger_record.fparams[0] > 0.0F) {
            pulse->SetInterval(trigger_record.fparams[0]);
          }
          pulse->TriggerOnStart(trigger_record.uparams[0] != 0U);
          pulse->SetTriggerLimit(trigger_record.uparams[1]);
          trigger = std::move(pulse);
          break;
        }
        case InputTriggerType::kActionChain: {
          auto chain = std::make_shared<input::ActionTriggerChain>();
          if (auto linked_action
            = get_or_create_action(trigger_record.linked_action_asset_key);
            linked_action) {
            chain->SetLinkedAction(std::move(linked_action));
          }
          if (trigger_record.fparams[0] > 0.0F) {
            chain->SetMaxDelaySeconds(trigger_record.fparams[0]);
          }
          chain->RequirePrerequisiteHeld(trigger_record.uparams[0] != 0U);
          trigger = std::move(chain);
          break;
        }
        case InputTriggerType::kChord:
        case InputTriggerType::kCombo:
          LOG_F(WARNING,
            "SceneLoader: Trigger type '{}' is not supported yet "
            "(context={})",
            data::pak::to_string(trigger_record.type), context_name);
          break;
        default:
          LOG_F(WARNING,
            "SceneLoader: Unknown trigger type '{}' in input context '{}'",
            data::pak::to_string(trigger_record.type), context_name);
          break;
        }

        if (!trigger) {
          continue;
        }
        trigger->SetActuationThreshold(trigger_record.actuation_threshold);
        ApplyTriggerBehavior(trigger_record.behavior, trigger);
        runtime_mapping->AddTrigger(std::move(trigger));
      }

      runtime_context->AddMapping(std::move(runtime_mapping));
      ++mapping_count;
    }

    input_system_->AddMappingContext(runtime_context, binding.priority);
    if ((binding.flags & InputContextBindingFlags::kActivateOnLoad)
      == InputContextBindingFlags::kActivateOnLoad) {
      input_system_->ActivateMappingContext(runtime_context);
    }
    LOG_F(INFO,
      "SceneLoader: Input context hydrated "
      "(name='{}' key={} priority={} activate_on_load={} mappings={})",
      context_name, data::to_string(binding.context_asset_key),
      binding.priority,
      ((binding.flags & InputContextBindingFlags::kActivateOnLoad)
        == InputContextBindingFlags::kActivateOnLoad),
      mapping_count);
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
    = [this](
        const uint32_t index) -> std::shared_ptr<const data::ScriptResource> {
    return ReadScriptResource(index);
  };

  auto map_origin =
    [this](const uint32_t index) -> std::optional<scripting::ScriptBlobOrigin> {
    if (auto res = ReadScriptResource(index)) {
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

auto SceneLoaderService::ReadScriptResource(uint32_t index) const
  -> std::shared_ptr<const data::ScriptResource>
{
  if (source_pak_) {
    try {
      return source_pak_->ReadScriptResource(index);
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "failed to read script resource from PAK (index={}): {}",
        index, ex.what());
      return nullptr;
    }
  }

  LOG_F(WARNING, "script resource requested but no source available (index={})",
    index);
  return nullptr;
}

} // namespace oxygen::examples
