//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPageAddressing.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::renderer::vsm {

inline constexpr std::size_t kMaxPhysicalPoolSliceRoles = 4;

enum class VsmPhysicalPoolSliceRole : std::uint8_t {
  kDynamicDepth = 0,
  kStaticDepth = 1,
};

using VsmPhysicalPoolSliceRoleList
  = oxygen::StaticVector<VsmPhysicalPoolSliceRole, kMaxPhysicalPoolSliceRoles>;

OXGN_RNDR_NDAPI auto to_string(VsmPhysicalPoolSliceRole value) noexcept -> const
  char*;

enum class VsmPhysicalPoolCompatibilityResult : std::uint8_t {
  kCompatible = 0,
  kUnavailable = 1,
  kPageSizeMismatch = 2,
  kTileCapacityMismatch = 3,
  kDepthFormatMismatch = 4,
  kSliceCountMismatch = 5,
  kSliceRolesMismatch = 6,
  kInvalidCurrentConfig = 7,
  kInvalidRequestedConfig = 8,
};

OXGN_RNDR_NDAPI auto to_string(
  VsmPhysicalPoolCompatibilityResult value) noexcept -> const char*;

enum class VsmPhysicalPoolConfigValidationResult : std::uint8_t {
  kValid = 0,
  kZeroPageSize = 1,
  kZeroTileCapacity = 2,
  kZeroSliceCount = 3,
  kInvalidDepthFormat = 4,
  kSliceRoleCountMismatch = 5,
  kUnsupportedSliceRoleCount = 6,
  kDuplicateSliceRole = 7,
  kMissingDynamicSlice = 8,
  kInvalidStaticSliceLayout = 9,
  kNonSquarePerSliceLayout = 10,
};

OXGN_RNDR_NDAPI auto to_string(
  VsmPhysicalPoolConfigValidationResult value) noexcept -> const char*;

enum class VsmPhysicalPoolChangeResult : std::uint8_t {
  kUnchanged = 0,
  kCreated = 1,
  kRecreated = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmPhysicalPoolChangeResult value) noexcept
  -> const char*;

enum class VsmHzbPoolChangeResult : std::uint8_t {
  kUnchanged = 0,
  kCreated = 1,
  kRecreated = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmHzbPoolChangeResult value) noexcept -> const
  char*;

enum class VsmHzbPoolConfigValidationResult : std::uint8_t {
  kValid = 0,
  kZeroMipCount = 1,
  kInvalidFormat = 2,
};

OXGN_RNDR_NDAPI auto to_string(VsmHzbPoolConfigValidationResult value) noexcept
  -> const char*;

struct VsmPhysicalPoolConfig {
  std::uint32_t page_size_texels { 0 };
  // Total page count across all slices. Each slice must resolve to a square
  // page grid via `physical_tile_capacity / array_slice_count`.
  std::uint32_t physical_tile_capacity { 0 };
  std::uint32_t array_slice_count { 0 };
  Format depth_format { Format::kUnknown };
  VsmPhysicalPoolSliceRoleList slice_roles {};
  std::string debug_name {};

  auto operator==(const VsmPhysicalPoolConfig&) const -> bool = default;
};

struct VsmHzbPoolConfig {
  // HZB width, height, and array size are derived from the active shadow pool.
  std::uint32_t mip_count { 0 };
  Format format { Format::kUnknown };
  std::string debug_name {};

  auto operator==(const VsmHzbPoolConfig&) const -> bool = default;
};

// Copyable per-frame snapshot of a physical pool's configuration and GPU
// resource handles. The shared_ptr members extend GPU resource lifetime through
// shared ownership — do not hold snapshots across pool resets or the GPU
// resources will remain alive after the pool manager releases them.
struct VsmPhysicalPoolSnapshot {
  std::uint64_t pool_identity { 0 };
  std::uint32_t page_size_texels { 0 };
  std::uint32_t tile_capacity { 0 };
  std::uint32_t tiles_per_axis { 0 };
  std::uint32_t slice_count { 0 };
  Format depth_format { Format::kUnknown };
  VsmPhysicalPoolSliceRoleList slice_roles {};
  std::shared_ptr<const graphics::Texture> shadow_texture {};
  std::shared_ptr<const graphics::Buffer> metadata_buffer {};
  bool is_available { false };

  auto operator==(const VsmPhysicalPoolSnapshot&) const -> bool = default;
};

// Copyable per-frame snapshot of the HZB pool. Same lifetime contract as
// VsmPhysicalPoolSnapshot — do not hold across pool resets.
struct VsmHzbPoolSnapshot {
  std::uint32_t width { 0 };
  std::uint32_t height { 0 };
  std::uint32_t mip_count { 0 };
  std::uint32_t array_size { 0 };
  Format format { Format::kUnknown };
  std::shared_ptr<const graphics::Texture> texture {};
  bool is_available { false };

  auto operator==(const VsmHzbPoolSnapshot&) const -> bool = default;
};

OXGN_RNDR_NDAPI auto Validate(const VsmPhysicalPoolConfig& config) noexcept
  -> VsmPhysicalPoolConfigValidationResult;
OXGN_RNDR_NDAPI auto IsValid(const VsmPhysicalPoolConfig& config) noexcept
  -> bool;

OXGN_RNDR_NDAPI auto Validate(const VsmHzbPoolConfig& config) noexcept
  -> VsmHzbPoolConfigValidationResult;
OXGN_RNDR_NDAPI auto IsValid(const VsmHzbPoolConfig& config) noexcept -> bool;

} // namespace oxygen::renderer::vsm
