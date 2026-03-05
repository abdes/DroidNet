//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

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
    case ComponentType::kUnknown:            return "__Unknown__";
    case ComponentType::kRenderable:         return "MESH";
    case ComponentType::kPerspectiveCamera:  return "PCAM";
    case ComponentType::kOrthographicCamera: return "OCAM";
    case ComponentType::kDirectionalLight:   return "DLIT";
    case ComponentType::kPointLight:         return "PLIT";
    case ComponentType::kSpotLight:          return "SLIT";
    case ComponentType::kScripting:          return "SCRP";
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

  auto check_and_append = [&](UsageFlags flag, const char* name) {
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
    // clang-format on
  }
  return "__NotSupported__";
}

auto oxygen::data::pak::scripting::to_string(
  const oxygen::data::pak::scripting::ScriptLanguage value) noexcept
  -> std::string_view
{
  switch (value) {
    // clang-format off
    case ScriptLanguage::kLuau: return "Luau";
    // clang-format on
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
  auto check_and_append
    = [&](const Flags flag, const char* name) {
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
  const oxygen::data::pak::scripting::ScriptingComponentFlags value) noexcept
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
  auto check_and_append
    = [&](const Flags flag, const char* name) {
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
  const oxygen::data::pak::scripting::ScriptSlotFlags value) noexcept
  -> std::string
{
  using Flags = oxygen::data::pak::scripting::ScriptSlotFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  auto checked = Flags::kNone;

  [[maybe_unused]]
  auto check_and_append
    = [&](const Flags flag, const char* name) {
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

  const auto check_and_append = [&](const Flags flag, const char* name) {
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
  DCHECK_EQ_F(checked, value,
    "to_string: Unchecked InputActionAssetFlags value detected");
  return result;
}

auto oxygen::data::pak::input::to_string(
  const oxygen::data::pak::input::InputMappingContextFlags value) -> std::string
{
  using Flags = oxygen::data::pak::input::InputMappingContextFlags;

  if (value == Flags::kNone) {
    return "None";
  }

  std::string result;
  bool first = true;
  [[maybe_unused]] auto checked = Flags::kNone;

  const auto check_and_append = [&](const Flags flag, const char* name) {
    if ((value & flag) == flag) {
      if (!first) {
        result += " | ";
      }
      result += name;
      first = false;
      checked |= flag;
    }
  };

  check_and_append(Flags::kAutoLoad, "AutoLoad");
  check_and_append(Flags::kAutoActivate, "AutoActivate");
  DCHECK_EQ_F(checked, value,
    "to_string: Unchecked InputMappingContextFlags value detected");
  return result;
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

  const auto check_and_append = [&](const Flags flag, const char* name) {
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
