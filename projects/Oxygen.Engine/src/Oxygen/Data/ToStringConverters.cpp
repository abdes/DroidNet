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
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MeshType.h>

auto oxygen::data::to_string(oxygen::data::AssetType value) noexcept -> const
  char*
{
  switch (value) {
    // clang-format off
    case AssetType::kUnknown:      return "__Unknown__";
    case AssetType::kMaterial:     return "Material";
    case AssetType::kGeometry:     return "Geometry";
    case AssetType::kScene:        return "Scene";
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
      if (!first)
        result += " | ";
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
