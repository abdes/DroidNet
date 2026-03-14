//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::renderer {

enum class VirtualShadowPageFlag : std::uint32_t {
  kAllocated = (1U << 0U),
  kDynamicUncached = (1U << 1U),
  kStaticUncached = (1U << 2U),
  kDetailGeometry = (1U << 3U),
  kUsedThisFrame = (1U << 4U),
  kHierarchyAllocatedDescendant = (1U << 5U),
  kHierarchyDynamicUncachedDescendant = (1U << 6U),
  kHierarchyStaticUncachedDescendant = (1U << 7U),
  kHierarchyDetailDescendant = (1U << 8U),
  kHierarchyUsedThisFrameDescendant = (1U << 9U),
};

[[nodiscard]] constexpr auto ToMask(const VirtualShadowPageFlag flag)
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(flag);
}

[[nodiscard]] constexpr auto MakeVirtualShadowPageFlags(
  const bool allocated = false, const bool dynamic_uncached = false,
  const bool static_uncached = false, const bool detail_geometry = false,
  const bool used_this_frame = false) -> std::uint32_t
{
  return (allocated ? ToMask(VirtualShadowPageFlag::kAllocated) : 0U)
    | (dynamic_uncached ? ToMask(VirtualShadowPageFlag::kDynamicUncached) : 0U)
    | (static_uncached ? ToMask(VirtualShadowPageFlag::kStaticUncached) : 0U)
    | (detail_geometry ? ToMask(VirtualShadowPageFlag::kDetailGeometry) : 0U)
    | (used_this_frame ? ToMask(VirtualShadowPageFlag::kUsedThisFrame) : 0U);
}

[[nodiscard]] constexpr auto HasVirtualShadowPageFlag(
  const std::uint32_t page_flags, const VirtualShadowPageFlag flag) -> bool
{
  return (page_flags & ToMask(flag)) != 0U;
}

[[nodiscard]] constexpr auto MakeVirtualShadowHierarchyFlags(
  const std::uint32_t page_flags) -> std::uint32_t
{
  const bool any_allocated
    = HasVirtualShadowPageFlag(page_flags, VirtualShadowPageFlag::kAllocated)
    || HasVirtualShadowPageFlag(page_flags,
      VirtualShadowPageFlag::kHierarchyAllocatedDescendant);
  const bool any_dynamic_uncached = HasVirtualShadowPageFlag(
                                      page_flags,
                                      VirtualShadowPageFlag::kDynamicUncached)
    || HasVirtualShadowPageFlag(page_flags,
      VirtualShadowPageFlag::kHierarchyDynamicUncachedDescendant);
  const bool any_static_uncached = HasVirtualShadowPageFlag(
                                     page_flags,
                                     VirtualShadowPageFlag::kStaticUncached)
    || HasVirtualShadowPageFlag(page_flags,
      VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant);
  const bool any_detail = HasVirtualShadowPageFlag(
                            page_flags, VirtualShadowPageFlag::kDetailGeometry)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kHierarchyDetailDescendant);
  const bool any_used = HasVirtualShadowPageFlag(
                          page_flags, VirtualShadowPageFlag::kUsedThisFrame)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant);

  return (any_allocated
            ? ToMask(VirtualShadowPageFlag::kHierarchyAllocatedDescendant)
            : 0U)
    | (any_dynamic_uncached
          ? ToMask(
            VirtualShadowPageFlag::kHierarchyDynamicUncachedDescendant)
          : 0U)
    | (any_static_uncached
          ? ToMask(VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant)
          : 0U)
    | (any_detail
          ? ToMask(VirtualShadowPageFlag::kHierarchyDetailDescendant)
          : 0U)
    | (any_used
          ? ToMask(VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant)
          : 0U);
}

[[nodiscard]] constexpr auto MergeVirtualShadowHierarchyFlags(
  const std::uint32_t base_flags, const std::uint32_t child_page_flags)
  -> std::uint32_t
{
  return base_flags | MakeVirtualShadowHierarchyFlags(child_page_flags);
}

[[nodiscard]] constexpr auto
VirtualShadowPageFlagsStructuralCoherenceMask() -> std::uint32_t
{
  return ToMask(VirtualShadowPageFlag::kAllocated)
    | ToMask(VirtualShadowPageFlag::kDynamicUncached)
    | ToMask(VirtualShadowPageFlag::kStaticUncached)
    | ToMask(VirtualShadowPageFlag::kHierarchyAllocatedDescendant)
    | ToMask(VirtualShadowPageFlag::kHierarchyDynamicUncachedDescendant)
    | ToMask(VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant);
}

[[nodiscard]] constexpr auto NormalizeVirtualShadowPageFlagsForStructuralCoherence(
  const std::uint32_t page_flags) -> std::uint32_t
{
  return page_flags & VirtualShadowPageFlagsStructuralCoherenceMask();
}

[[nodiscard]] constexpr auto VirtualShadowPageFlagsStructurallyEqual(
  const std::uint32_t lhs, const std::uint32_t rhs) -> bool
{
  return NormalizeVirtualShadowPageFlagsForStructuralCoherence(lhs)
    == NormalizeVirtualShadowPageFlagsForStructuralCoherence(rhs);
}

[[nodiscard]] constexpr auto HasVirtualShadowHierarchyVisibility(
  const std::uint32_t page_flags) -> bool
{
  return HasVirtualShadowPageFlag(page_flags, VirtualShadowPageFlag::kAllocated)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kUsedThisFrame)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kDetailGeometry)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kHierarchyAllocatedDescendant)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant)
    || HasVirtualShadowPageFlag(
      page_flags, VirtualShadowPageFlag::kHierarchyDetailDescendant);
}

} // namespace oxygen::renderer
