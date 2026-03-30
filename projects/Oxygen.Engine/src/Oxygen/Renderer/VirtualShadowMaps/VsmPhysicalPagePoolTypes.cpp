//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolTypes.h>

namespace oxygen::renderer::vsm {

auto to_string(const VsmPhysicalPoolSliceRole value) noexcept -> const char*
{
  switch (value) {
  case VsmPhysicalPoolSliceRole::kDynamicDepth:
    return "DynamicDepth";
  case VsmPhysicalPoolSliceRole::kStaticDepth:
    return "StaticDepth";
  }

  return "__NotSupported__";
}

auto to_string(const VsmPhysicalPoolCompatibilityResult value) noexcept -> const
  char*
{
  switch (value) {
  case VsmPhysicalPoolCompatibilityResult::kCompatible:
    return "Compatible";
  case VsmPhysicalPoolCompatibilityResult::kUnavailable:
    return "Unavailable";
  case VsmPhysicalPoolCompatibilityResult::kPageSizeMismatch:
    return "PageSizeMismatch";
  case VsmPhysicalPoolCompatibilityResult::kTileCapacityMismatch:
    return "TileCapacityMismatch";
  case VsmPhysicalPoolCompatibilityResult::kDepthFormatMismatch:
    return "DepthFormatMismatch";
  case VsmPhysicalPoolCompatibilityResult::kSliceCountMismatch:
    return "SliceCountMismatch";
  case VsmPhysicalPoolCompatibilityResult::kSliceRolesMismatch:
    return "SliceRolesMismatch";
  case VsmPhysicalPoolCompatibilityResult::kInvalidCurrentConfig:
    return "InvalidCurrentConfig";
  case VsmPhysicalPoolCompatibilityResult::kInvalidRequestedConfig:
    return "InvalidRequestedConfig";
  }

  return "__NotSupported__";
}

auto to_string(const VsmPhysicalPoolConfigValidationResult value) noexcept
  -> const char*
{
  switch (value) {
  case VsmPhysicalPoolConfigValidationResult::kValid:
    return "Valid";
  case VsmPhysicalPoolConfigValidationResult::kZeroPageSize:
    return "ZeroPageSize";
  case VsmPhysicalPoolConfigValidationResult::kZeroTileCapacity:
    return "ZeroTileCapacity";
  case VsmPhysicalPoolConfigValidationResult::kZeroSliceCount:
    return "ZeroSliceCount";
  case VsmPhysicalPoolConfigValidationResult::kInvalidDepthFormat:
    return "InvalidDepthFormat";
  case VsmPhysicalPoolConfigValidationResult::kSliceRoleCountMismatch:
    return "SliceRoleCountMismatch";
  case VsmPhysicalPoolConfigValidationResult::kUnsupportedSliceRoleCount:
    return "UnsupportedSliceRoleCount";
  case VsmPhysicalPoolConfigValidationResult::kDuplicateSliceRole:
    return "DuplicateSliceRole";
  case VsmPhysicalPoolConfigValidationResult::kMissingDynamicSlice:
    return "MissingDynamicSlice";
  case VsmPhysicalPoolConfigValidationResult::kInvalidStaticSliceLayout:
    return "InvalidStaticSliceLayout";
  case VsmPhysicalPoolConfigValidationResult::kNonSquarePerSliceLayout:
    return "NonSquarePerSliceLayout";
  }

  return "__NotSupported__";
}

auto to_string(const VsmPhysicalPoolChangeResult value) noexcept -> const char*
{
  switch (value) {
  case VsmPhysicalPoolChangeResult::kUnchanged:
    return "Unchanged";
  case VsmPhysicalPoolChangeResult::kCreated:
    return "Created";
  case VsmPhysicalPoolChangeResult::kRecreated:
    return "Recreated";
  }

  return "__NotSupported__";
}

auto to_string(const VsmHzbPoolChangeResult value) noexcept -> const char*
{
  switch (value) {
  case VsmHzbPoolChangeResult::kUnchanged:
    return "Unchanged";
  case VsmHzbPoolChangeResult::kCreated:
    return "Created";
  case VsmHzbPoolChangeResult::kRecreated:
    return "Recreated";
  }

  return "__NotSupported__";
}

auto to_string(const VsmHzbPoolConfigValidationResult value) noexcept -> const
  char*
{
  switch (value) {
  case VsmHzbPoolConfigValidationResult::kValid:
    return "Valid";
  case VsmHzbPoolConfigValidationResult::kZeroMipCount:
    return "ZeroMipCount";
  case VsmHzbPoolConfigValidationResult::kInvalidFormat:
    return "InvalidFormat";
  }

  return "__NotSupported__";
}

auto Validate(const VsmPhysicalPoolConfig& config) noexcept
  -> VsmPhysicalPoolConfigValidationResult
{
  if (config.page_size_texels == 0) {
    return VsmPhysicalPoolConfigValidationResult::kZeroPageSize;
  }
  if (config.physical_tile_capacity == 0) {
    return VsmPhysicalPoolConfigValidationResult::kZeroTileCapacity;
  }
  if (config.array_slice_count == 0) {
    return VsmPhysicalPoolConfigValidationResult::kZeroSliceCount;
  }
  if (config.depth_format == Format::kUnknown) {
    return VsmPhysicalPoolConfigValidationResult::kInvalidDepthFormat;
  }
  if (config.slice_roles.size() != config.array_slice_count) {
    return VsmPhysicalPoolConfigValidationResult::kSliceRoleCountMismatch;
  }
  if (config.slice_roles.size() > 2) {
    return VsmPhysicalPoolConfigValidationResult::kUnsupportedSliceRoleCount;
  }

  bool has_dynamic = false;
  bool has_static = false;
  for (const auto role : config.slice_roles) {
    switch (role) {
    case VsmPhysicalPoolSliceRole::kDynamicDepth:
      if (has_dynamic) {
        return VsmPhysicalPoolConfigValidationResult::kDuplicateSliceRole;
      }
      has_dynamic = true;
      break;
    case VsmPhysicalPoolSliceRole::kStaticDepth:
      if (has_static) {
        return VsmPhysicalPoolConfigValidationResult::kDuplicateSliceRole;
      }
      has_static = true;
      break;
    }
  }

  if (!has_dynamic) {
    return VsmPhysicalPoolConfigValidationResult::kMissingDynamicSlice;
  }

  if (config.slice_roles.size() == 2
    && (config.slice_roles[0] != VsmPhysicalPoolSliceRole::kDynamicDepth
      || config.slice_roles[1] != VsmPhysicalPoolSliceRole::kStaticDepth)) {
    return VsmPhysicalPoolConfigValidationResult::kInvalidStaticSliceLayout;
  }

  if (ComputeTilesPerAxis(
        config.physical_tile_capacity, config.array_slice_count)
    == 0) {
    return VsmPhysicalPoolConfigValidationResult::kNonSquarePerSliceLayout;
  }

  return VsmPhysicalPoolConfigValidationResult::kValid;
}

auto IsValid(const VsmPhysicalPoolConfig& config) noexcept -> bool
{
  return Validate(config) == VsmPhysicalPoolConfigValidationResult::kValid;
}

auto Validate(const VsmHzbPoolConfig& config) noexcept
  -> VsmHzbPoolConfigValidationResult
{
  if (config.mip_count == 0) {
    return VsmHzbPoolConfigValidationResult::kZeroMipCount;
  }
  if (config.format == Format::kUnknown) {
    return VsmHzbPoolConfigValidationResult::kInvalidFormat;
  }

  return VsmHzbPoolConfigValidationResult::kValid;
}

auto IsValid(const VsmHzbPoolConfig& config) noexcept -> bool
{
  return Validate(config) == VsmHzbPoolConfigValidationResult::kValid;
}

} // namespace oxygen::renderer::vsm
