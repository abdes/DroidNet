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

void SceneLoaderService::ValidateUnsupportedPhysicsDomains(
  const data::PhysicsSceneAsset& physics_asset) const
{
  const auto soft_body_bindings
    = physics_asset.GetBindings<data::pak::physics::SoftBodyBindingRecord>();
  const auto vehicle_bindings
    = physics_asset.GetBindings<data::pak::physics::VehicleBindingRecord>();
  const auto aggregate_bindings
    = physics_asset.GetBindings<data::pak::physics::AggregateBindingRecord>();

  if (!soft_body_bindings.empty()) {
    throw std::runtime_error(
      std::string("soft-body sidecar hydration is not implemented in "
                  "SceneLoader (requires shape/material resource resolution). "
                  "sidecar key=")
      + data::to_string(physics_asset.GetAssetKey())
      + " soft_body=" + std::to_string(soft_body_bindings.size()) + ")");
  }

  if (!vehicle_bindings.empty()) {
    throw std::runtime_error(
      std::string(
        "vehicle sidecar hydration is not implemented in SceneLoader "
        "(requires decoding vehicle topology/resources). sidecar key=")
      + data::to_string(physics_asset.GetAssetKey())
      + " vehicle=" + std::to_string(vehicle_bindings.size()) + ")");
  }

  if (!aggregate_bindings.empty()) {
    throw std::runtime_error(
      std::string(
        "aggregate sidecar hydration is not implemented in "
        "SceneLoader (aggregate membership encoding is not defined in "
        "PhysicsSceneAsset binding records). sidecar key=")
      + data::to_string(physics_asset.GetAssetKey())
      + " aggregate=" + std::to_string(aggregate_bindings.size()) + ")");
  }
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
    if (shape_desc.cooked_shape_ref.resource_index
        != data::pak::core::kNoResourceIndex
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
  if (cooked_ref.resource_index == data::pak::core::kNoResourceIndex) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-004: missing cooked_shape_ref.resource_index in ")
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
    *current_physics_context_asset_key_, cooked_ref.resource_index);
  if (!resource_key_opt.has_value()) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: failed to resolve physics resource key in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " resource_index="
      + std::to_string(cooked_ref.resource_index.get()) + ")");
  }

  auto resource = loader_.GetPhysicsResource(*resource_key_opt);
  if (!resource) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: physics payload resource not loaded in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " resource_index="
      + std::to_string(cooked_ref.resource_index.get()) + ")");
  }

  if (resource->GetFormat()
    != data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary) {
    throw std::runtime_error(
      std::string("OXY-SHAPE-007: unsupported physics resource format in ")
      + std::string(binding_kind) + " binding (node_index="
      + std::to_string(node_index) + " resource_index="
      + std::to_string(cooked_ref.resource_index.get()) + " format="
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

  for (const auto& record : bindings) {
    const auto node_index_a = static_cast<size_t>(record.node_index_a);
    const auto node_index_b = static_cast<size_t>(record.node_index_b);
    if (node_index_a >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("joint node_index_a out of range: ")
        + std::to_string(record.node_index_a));
    }
    if (record.node_index_b == data::pak::core::kNoResourceIndex) {
      throw std::runtime_error(
        std::string("joint world-anchor mode is not supported by SceneLoader "
                    "hydrator (node_index_a=")
        + std::to_string(record.node_index_a) + ")");
    }
    if (node_index_b >= runtime_nodes_.size()) {
      throw std::runtime_error(std::string("joint node_index_b out of range: ")
        + std::to_string(record.node_index_b));
    }
    if (record.constraint_resource_index != data::pak::core::kNoResourceIndex) {
      throw std::runtime_error(
        std::string("joint constraint_resource_index is not supported by "
                    "SceneLoader hydrator (node_index_a=")
        + std::to_string(record.node_index_a)
        + " node_index_b=" + std::to_string(record.node_index_b)
        + " index=" + std::to_string(record.constraint_resource_index) + ")");
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
    desc.body_a = body_a->GetBodyId();
    desc.body_b = body_b->GetBodyId();

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

    physics::body::BodyDesc desc {};
    desc.type = physics::body::BodyType::kStatic;
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
        desc.friction = (std::max)(0.0F, material_desc.friction);
        desc.restitution = (std::max)(0.0F, material_desc.restitution);
      }
    }

    auto& node = runtime_nodes_[node_index];
    desc.initial_position
      = node.GetTransform().GetLocalPosition().value_or(Vec3(0.0F));
    desc.initial_rotation = node.GetTransform().GetLocalRotation().value_or(
      Quat(1.0F, 0.0F, 0.0F, 0.0F));
    const auto attached = physics::ScenePhysics::AttachRigidBodyDetailed(
      observer_ptr<physics::PhysicsModule> { &physics_module }, node, desc);
    if (!attached.has_value()) {
      const auto reason_text
        = std::string(physics::to_string(attached.error()));
      throw std::runtime_error(
        std::string("failed to attach collider binding for node_index=")
        + std::to_string(record.node_index) + " reason=" + reason_text);
    }
  }
}

void SceneLoaderService::HydrateRigidBodyBindings(
  physics::PhysicsModule& physics_module,
  const std::span<const data::pak::physics::RigidBodyBindingRecord> bindings)
{
  for (const auto& record : bindings) {
    const auto node_index = static_cast<size_t>(record.node_index);
    if (node_index >= runtime_nodes_.size()) {
      throw std::runtime_error(
        std::string("rigid-body node_index out of range: ")
        + std::to_string(record.node_index));
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
    if (desc.gravity_factor > 0.0F) {
      desc.flags = desc.flags | physics::body::BodyFlags::kEnableGravity;
    }
    if (record.is_sensor != 0U) {
      desc.flags = desc.flags | physics::body::BodyFlags::kIsTrigger;
    }
    if (record.motion_quality
      == data::pak::physics::PhysicsMotionQuality::kLinearCast) {
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
        desc.friction = (std::max)(0.0F, material_desc.friction);
        desc.restitution = (std::max)(0.0F, material_desc.restitution);
        if (record.mass <= 0.0F
          && desc.type != physics::body::BodyType::kStatic) {
          desc.mass_kg = (std::max)(0.001F, material_desc.density);
        }
      }
    }

    auto& node = runtime_nodes_[node_index];
    desc.initial_position
      = node.GetTransform().GetLocalPosition().value_or(Vec3(0.0F));
    desc.initial_rotation = node.GetTransform().GetLocalRotation().value_or(
      Quat(1.0F, 0.0F, 0.0F, 0.0F));
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
          + " cooked_ref_index="
          + std::to_string(shape_desc.cooked_shape_ref.resource_index.get())
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

    auto& node = runtime_nodes_[node_index];
    desc.initial_position
      = node.GetTransform().GetLocalPosition().value_or(Vec3(0.0F));
    desc.initial_rotation = node.GetTransform().GetLocalRotation().value_or(
      Quat(1.0F, 0.0F, 0.0F, 0.0F));
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

void SceneLoaderService::HydratePhysicsBindings(
  const data::PhysicsSceneAsset& physics_asset)
{
  auto* physics_module = ResolvePhysicsModule().get();
  if (physics_module == nullptr) {
    throw std::runtime_error(
      "physics sidecar present but PhysicsModule is unavailable");
  }

  ValidateUnsupportedPhysicsDomains(physics_asset);

  const auto rigid_body_bindings
    = physics_asset.GetBindings<data::pak::physics::RigidBodyBindingRecord>();
  const auto collider_bindings
    = physics_asset.GetBindings<data::pak::physics::ColliderBindingRecord>();
  const auto character_bindings
    = physics_asset.GetBindings<data::pak::physics::CharacterBindingRecord>();
  const auto joint_bindings
    = physics_asset.GetBindings<data::pak::physics::JointBindingRecord>();

  HydrateRigidBodyBindings(*physics_module, rigid_body_bindings);
  HydrateColliderBindings(*physics_module, collider_bindings);
  HydrateCharacterBindings(*physics_module, character_bindings);
  HydrateJointBindings(*physics_module, joint_bindings);

  LOG_F(INFO,
    "SceneLoader: Physics hydration complete (rigid_bodies={} colliders={} "
    "characters={} joints={})",
    rigid_body_bindings.size(), collider_bindings.size(),
    character_bindings.size(), joint_bindings.size());
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
  SelectActiveCamera(asset);
  EnsureCameraAndViewport(scene);
  // Geometry pins are only needed until scene instantiation finishes.
  ReleasePinnedGeometryAssets();
  LogSceneHierarchy(scene);

  LOG_F(INFO, "SceneLoader: Runtime scene instantiation complete.");
  co_return std::move(active_camera_);
}

auto SceneLoaderService::PreloadPhysicsShapeResources(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<>
{
  const auto context_asset_key = physics_asset.GetAssetKey();

  std::unordered_set<uint32_t> payload_indices {};
  auto collect_payload_ref = [&](const data::AssetKey& shape_asset_key,
                               const std::string_view binding_kind,
                               const uint32_t node_index) {
    const auto shape_desc
      = ResolveCollisionShapeAsset(shape_asset_key, binding_kind, node_index);
    if (shape_desc.cooked_shape_ref.resource_index
      != data::pak::core::kNoResourceIndex) {
      payload_indices.insert(shape_desc.cooked_shape_ref.resource_index.get());
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

  for (const auto resource_index_raw : payload_indices) {
    const auto resource_index
      = data::pak::core::ResourceIndexT { resource_index_raw };
    const auto resource_key_opt = loader_.MakePhysicsResourceKeyForAsset(
      context_asset_key, resource_index);
    if (!resource_key_opt.has_value()) {
      throw std::runtime_error(
        std::string(
          "OXY-SHAPE-007: failed to resolve physics resource key for ")
        + std::to_string(resource_index_raw));
    }
    auto payload = co_await loader_.LoadPhysicsResourceAsync(*resource_key_opt);
    if (!payload) {
      throw std::runtime_error(
        std::string(
          "OXY-SHAPE-007: failed to preload physics payload resource ")
        + std::to_string(resource_index_raw));
    }
  }
}

auto SceneLoaderService::HydratePhysicsSidecar(
  const data::PhysicsSceneAsset& physics_asset) -> co::Co<>
{
  current_physics_context_asset_key_ = physics_asset.GetAssetKey();
  ScopeGuard clear_context(
    [this]() noexcept { current_physics_context_asset_key_.reset(); });
  co_await PreloadPhysicsShapeResources(physics_asset);
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
