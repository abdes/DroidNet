//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/ComponentType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>

auto oxygen::data::to_string(oxygen::data::AssetType value) noexcept -> const
  char*
{
  switch (value) {
    // clang-format off
    case AssetType::kUnknown:      return "__Unknown__";
    case AssetType::kMaterial:     return "Material";
    case AssetType::kGeometry:     return "Geometry";
    case AssetType::kScene:        return "Scene";
    case AssetType::kScript:       return "Script";
    case AssetType::kInputAction:  return "InputAction";
    case AssetType::kInputMappingContext: return "InputMappingContext";
    case AssetType::kPhysicsMaterial: return "PhysicsMaterial";
    case AssetType::kCollisionShape: return "CollisionShape";
    case AssetType::kPhysicsScene: return "PhysicsScene";
    // clang-format on
  }

  return "__NotSupported__";
}

auto oxygen::data::to_string(oxygen::data::MeshType value) noexcept -> const
  char*
{
  switch (value) {
    // clang-format off
    case MeshType::kUnknown:      return "__Unknown__";
    case MeshType::kStandard:     return "Standard";
    case MeshType::kProcedural:   return "Procedural";
    case MeshType::kSkinned:      return "Skinned";
    case MeshType::kMorphTarget:  return "MorphTarget";
    case MeshType::kInstanced:    return "Instanced";
    case MeshType::kCollision:    return "Collision";
    case MeshType::kNavigation:   return "Navigation";
    case MeshType::kBillboard:    return "Billboard";
    case MeshType::kVoxel:        return "Voxel";
    // clang-format on
  }

  return "__NotSupported__";
}

auto oxygen::data::to_string(oxygen::data::ComponentType value) noexcept
  -> const char*
{
  switch (value) {
    // clang-format off
    case ComponentType::kUnknown:              return "__Unknown__";
    case ComponentType::kRenderable:           return "MESH";
    case ComponentType::kPerspectiveCamera:    return "PCAM";
    case ComponentType::kOrthographicCamera:   return "OCAM";
    case ComponentType::kDirectionalLight:     return "DLIT";
    case ComponentType::kPointLight:           return "PLIT";
    case ComponentType::kSpotLight:            return "SLIT";
    case ComponentType::kScripting:            return "SCRP";
    case ComponentType::kSceneInstance:        return "INST";
    case ComponentType::kStartingInputContext: return "SICX";
    case ComponentType::kNodePath:             return "NPTH";
    case ComponentType::kNodePathElement:      return "NPEL";
    case ComponentType::kScriptParamOverride:  return "SPOV";
    case ComponentType::kLocalFogVolume:       return "LFOG";
    case ComponentType::kOverride:             return "OVRD";
    case ComponentType::kOverrideLodPolicy:    return "OLOD";
    // clang-format on
  }

  return "__NotSupported__";
}

auto oxygen::data::to_string(oxygen::data::MaterialDomain value) noexcept
  -> const char*
{
  switch (value) {
    // clang-format off
    case MaterialDomain::kUnknown:        return "__Unknown__";
    case MaterialDomain::kOpaque:         return "Opaque";
    case MaterialDomain::kAlphaBlended:   return "Alpha Blended";
    case MaterialDomain::kMasked:         return "Masked";
    case MaterialDomain::kDecal:          return "Decal";
    case MaterialDomain::kUserInterface:  return "User Interface";
    case MaterialDomain::kPostProcess:    return "Post-Process";
    // clang-format on
  }

  return "__NotSupported__";
}

// Returns a string representation of UsageFlags bitmask.
auto oxygen::data::to_string(oxygen::data::BufferResource::UsageFlags value)
  -> std::string
{
  using UsageFlags = oxygen::data::BufferResource::UsageFlags;

  if (value == UsageFlags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = UsageFlags::kNone;

  auto check_and_append = [&](UsageFlags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  // --- Buffer Role Flags (can be combined) ---
  check_and_append(UsageFlags::kVertexBuffer, "VertexBuffer");
  check_and_append(UsageFlags::kIndexBuffer, "IndexBuffer");
  check_and_append(UsageFlags::kConstantBuffer, "ConstantBuffer");
  check_and_append(UsageFlags::kStorageBuffer, "StorageBuffer");
  check_and_append(UsageFlags::kIndirectBuffer, "IndirectBuffer");

  // --- CPU Access Flags (can be combined) ---
  check_and_append(UsageFlags::kCPUWritable, "CPUWritable");
  check_and_append(UsageFlags::kCPUReadable, "CPUReadable");

  // --- Update Frequency Flags (mutually exclusive) ---
  check_and_append(UsageFlags::kDynamic, "Dynamic");
  check_and_append(UsageFlags::kStatic, "Static");
  check_and_append(UsageFlags::kImmutable, "Immutable");

  DCHECK_EQ_F(checked, value, "to_string: Unchecked UsageFlags value detected");

  return result;
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptParamType value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case ScriptParamType::kNone:   return "None";
    case ScriptParamType::kBool:   return "Bool";
    case ScriptParamType::kInt32:  return "Int32";
    case ScriptParamType::kFloat:  return "Float";
    case ScriptParamType::kString: return "String";
    case ScriptParamType::kVec2:   return "Vec2";
    case ScriptParamType::kVec3:   return "Vec3";
    case ScriptParamType::kVec4:   return "Vec4";
    case ScriptParamType::kAsset:  return "Asset";
    case ScriptParamType::kNode:   return "Node";
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptLanguage value) noexcept
  -> std::string_view
{
  if (value == ScriptLanguage::kLuau) {
    return "Luau";
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptEncoding value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case ScriptEncoding::kBytecode: return "Bytecode";
    case ScriptEncoding::kSource:   return "Source";
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptCompression value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case ScriptCompression::kNone: return "None";
    case ScriptCompression::kZstd: return "Zstd";
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptAssetFlags value) -> std::string
{
  using Flags = oxygen::data::pak::scripting::ScriptAssetFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  [[maybe_unused]] bool first = true;
  auto checked = Flags::kNone;

  [[maybe_unused]]
  auto check_and_append = [&](const Flags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Flags::kAllowExternalSource, "AllowExternalSource");
  DCHECK_EQ_F(
    checked, value, "to_string: Unchecked ScriptAssetFlags value detected");

  return result;
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptingComponentFlags value)
  -> std::string
{
  using Flags = oxygen::data::pak::scripting::ScriptingComponentFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  [[maybe_unused]] bool first = true;
  auto checked = Flags::kNone;

  [[maybe_unused]]
  auto check_and_append = [&](const Flags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  // Add new flag names here when ScriptingComponentFlags grows.
  DCHECK_EQ_F(checked, value,
    "to_string: Unchecked ScriptingComponentFlags value detected");

  return result;
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptSlotFlags value) -> std::string
{
  using Flags = oxygen::data::pak::scripting::ScriptSlotFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = Flags::kNone;

  [[maybe_unused]]
  auto check_and_append = [&](const Flags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  // Add new flag names here when ScriptSlotFlags grows.
  DCHECK_EQ_F(
    checked, value, "to_string: Unchecked ScriptSlotFlags value detected");

  return result;
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputTriggerType value) noexcept
  -> std::string_view
{
  switch (value) {
  case InputTriggerType::kPressed:
    return "Pressed";
  case InputTriggerType::kReleased:
    return "Released";
  case InputTriggerType::kDown:
    return "Down";
  case InputTriggerType::kHold:
    return "Hold";
  case InputTriggerType::kHoldAndRelease:
    return "HoldAndRelease";
  case InputTriggerType::kPulse:
    return "Pulse";
  case InputTriggerType::kTap:
    return "Tap";
  case InputTriggerType::kChord:
    return "Chord";
  case InputTriggerType::kActionChain:
    return "ActionChain";
  case InputTriggerType::kCombo:
    return "Combo";
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputTriggerBehavior value) noexcept
  -> std::string_view
{
  switch (value) {
  case InputTriggerBehavior::kExplicit:
    return "Explicit";
  case InputTriggerBehavior::kImplicit:
    return "Implicit";
  case InputTriggerBehavior::kBlocker:
    return "Blocker";
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputActionAssetFlags value) -> std::string
{
  using Flags = oxygen::data::pak::input::InputActionAssetFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  [[maybe_unused]] auto checked = Flags::kNone;

  const auto check_and_append
    = [&](const Flags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Flags::kConsumesInput, "ConsumesInput");
  check_and_append(Flags::kCumulativeAccumulation, "CumulativeAccumulation");
  DCHECK_EQ_F(checked, value,
    "to_string: Unchecked InputActionAssetFlags value detected");
  return result;
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputMappingContextFlags value) -> std::string
{
  // No context flag is defined; any set bit is reserved.
  if (value == oxygen::data::pak::input::InputMappingContextFlags::kNone) {
    return "None";
  }
  return "Reserved(" + std::to_string(static_cast<uint32_t>(value)) + ")";
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputMappingFlags value) -> std::string
{
  using Flags = oxygen::data::pak::input::InputMappingFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  [[maybe_unused]] auto checked = Flags::kNone;
  DCHECK_EQ_F(
    checked, value, "to_string: Unchecked InputMappingFlags value detected");
  return result;
}

namespace {

//! A catalog name, such as `kRigidBody`, without its `k`.
constexpr auto CatalogName(const std::string_view name) noexcept
  -> std::string_view
{
  return name.substr(1);
}

} // namespace

// Each enum below is named from the catalog that defines its values, so a
// value added to a catalog is named with it.
// NOLINTBEGIN(*-macro-usage)
#define OXPHYS_BACKEND(name, value)                                            \
  case PhysicsBackend::name:                                                   \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(const PhysicsBackend value) noexcept
  -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_BACKEND

#define OXPHYS_RESOURCE_FORMAT(name, value)                                    \
  case PhysicsResourceFormat::name:                                            \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const PhysicsResourceFormat value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_RESOURCE_FORMAT

#define OXPHYS_COMBINE_MODE(name, value)                                       \
  case PhysicsCombineMode::name:                                               \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const PhysicsCombineMode value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_COMBINE_MODE

#define OXPHYS_SHAPE_TYPE(name, value)                                         \
  case ShapeType::name:                                                        \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(const ShapeType value) noexcept
  -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_SHAPE_TYPE

#define OXPHYS_SHAPE_PAYLOAD_TYPE(name, value)                                 \
  case ShapePayloadType::name:                                                 \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const ShapePayloadType value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_SHAPE_PAYLOAD_TYPE

#define OXPHYS_WORLD_BOUNDARY_MODE(name, value)                                \
  case WorldBoundaryMode::name:                                                \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const WorldBoundaryMode value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysicsShape.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_WORLD_BOUNDARY_MODE

#define OXPHYS_BODY_TYPE(name, value)                                          \
  case PhysicsBodyType::name:                                                  \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(const PhysicsBodyType value) noexcept
  -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_BODY_TYPE

#define OXPHYS_MOTION_QUALITY(name, value)                                     \
  case PhysicsMotionQuality::name:                                             \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const PhysicsMotionQuality value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_MOTION_QUALITY

#define OXPHYS_SOFT_BODY_TETHER_MODE(name, value)                              \
  case SoftBodyTetherMode::name:                                               \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const SoftBodyTetherMode value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_SOFT_BODY_TETHER_MODE

#define OXPHYS_VEHICLE_CONTROLLER_TYPE(name, value)                            \
  case VehicleControllerType::name:                                            \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const VehicleControllerType value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_VEHICLE_CONTROLLER_TYPE

#define OXPHYS_VEHICLE_WHEEL_SIDE(name, value)                                 \
  case VehicleWheelSide::name:                                                 \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const VehicleWheelSide value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_VEHICLE_WHEEL_SIDE

#define OXPHYS_AGGREGATE_AUTHORITY(name, value)                                \
  case AggregateAuthority::name:                                               \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const AggregateAuthority value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_AGGREGATE_AUTHORITY

#define OXPHYS_BINDING_TYPE(name, value)                                       \
  case PhysicsBindingType::name:                                               \
    return CatalogName(#name);
auto oxygen::data::pak::physics::to_string(
  const PhysicsBindingType value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Physics/PakPhysics.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXPHYS_BINDING_TYPE

#define OXNPUT_ACTION_VALUE_TYPE(name, value)                                  \
  case InputActionValueType::name:                                             \
    return CatalogName(#name);
auto oxygen::data::pak::input::to_string(
  const InputActionValueType value) noexcept -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Input/ActionValue.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXNPUT_ACTION_VALUE_TYPE

#define OXSCN_SKY_LIGHT_SOURCE(name, value)                                    \
  case SkyLightSource::name:                                                   \
    return CatalogName(#name);
auto oxygen::data::pak::world::to_string(const SkyLightSource value) noexcept
  -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Scene/Environment.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXSCN_SKY_LIGHT_SOURCE

#define OXSCN_SKY_SPHERE_SOURCE(name, value)                                   \
  case SkySphereSource::name:                                                  \
    return CatalogName(#name);
auto oxygen::data::pak::world::to_string(const SkySphereSource value) noexcept
  -> std::string_view
{
  switch (value) {
#include <Oxygen/Core/Meta/Scene/Environment.inc> // NOLINT
  }
  return "__NotSupported__";
}
#undef OXSCN_SKY_SPHERE_SOURCE

// NOLINTEND(*-macro-usage)

auto oxygen::data::loose_cooked::to_string(
  const oxygen::data::loose_cooked::IndexFlags value) -> std::string
{
  using Flags = oxygen::data::loose_cooked::IndexFlags;

  if (value == static_cast<Flags>(0)) {
    return "None";
  }

  std::string result;
  bool first = true;
  [[maybe_unused]] auto checked = static_cast<Flags>(0);

  const auto check_and_append
    = [&](const Flags flag, const char* name) -> void {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Flags::kHasVirtualPaths, "HasVirtualPaths");
  check_and_append(Flags::kHasFileRecords, "HasFileRecords");

  DCHECK_EQ_F(checked, value, "to_string: Unchecked IndexFlags value detected");
  return result;
}

auto oxygen::data::pak::world::to_string(
  const oxygen::data::pak::world::EnvironmentComponentType value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case EnvironmentComponentType::kSkyAtmosphere:     return "SkyAtmosphere";
    case EnvironmentComponentType::kVolumetricClouds:  return "VolumetricClouds";
    case EnvironmentComponentType::kFog:               return "Fog";
    case EnvironmentComponentType::kSkyLight:          return "SkyLight";
    case EnvironmentComponentType::kSkySphere:         return "SkySphere";
    case EnvironmentComponentType::kPostProcessVolume: return "PostProcessVolume";
    case EnvironmentComponentType::kBackground:        return "Background";
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::geometry::to_string(
  const oxygen::data::pak::geometry::LodPolicyType value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case LodPolicyType::kFixed:            return "Fixed";
    case LodPolicyType::kDistance:         return "Distance";
    case LodPolicyType::kScreenSpaceError: return "ScreenSpaceError";
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::render::to_string(
  const oxygen::data::pak::render::TexturePackingPolicyId value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case TexturePackingPolicyId::kD3D12:       return "D3D12";
    case TexturePackingPolicyId::kTightPacked: return "TightPacked";
    // clang-format on
  }
  return "__NotSupported__";
}
