//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/Types/VirtualShadowPageFlags.h>

namespace {

using oxygen::renderer::HasVirtualShadowHierarchyVisibility;
using oxygen::renderer::HasVirtualShadowPageFlag;
using oxygen::renderer::MakeVirtualShadowHierarchyFlags;
using oxygen::renderer::MakeVirtualShadowPageFlags;
using oxygen::renderer::MergeVirtualShadowHierarchyFlags;
using oxygen::renderer::NormalizeVirtualShadowPageFlagsForStructuralCoherence;
using oxygen::renderer::VirtualShadowPageFlagsStructurallyEqual;
using oxygen::renderer::VirtualShadowPageFlag;

TEST(VirtualShadowFlagsContractsTest,
  PageFlagsHelpersExposeBinaryCoarseDetailPolicy)
{
  constexpr auto flags
    = MakeVirtualShadowPageFlags(true, true, false, true, true);

  EXPECT_TRUE(HasVirtualShadowPageFlag(flags, VirtualShadowPageFlag::kAllocated));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kDynamicUncached));
  EXPECT_FALSE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kStaticUncached));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kDetailGeometry));
  EXPECT_TRUE(HasVirtualShadowPageFlag(
    flags, VirtualShadowPageFlag::kUsedThisFrame));
}

TEST(VirtualShadowFlagsContractsTest,
  HierarchicalPageFlagsPropagateDescendantUsage)
{
  constexpr auto child_flags
    = MakeVirtualShadowPageFlags(true, true, false, true, true);
  constexpr auto propagated_hierarchy
    = MakeVirtualShadowHierarchyFlags(child_flags);
  constexpr auto parent_flags
    = MergeVirtualShadowHierarchyFlags(0U, child_flags);

  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyAllocatedDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyDynamicUncachedDescendant));
  EXPECT_FALSE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyStaticUncachedDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyDetailDescendant));
  EXPECT_TRUE(HasVirtualShadowPageFlag(propagated_hierarchy,
    VirtualShadowPageFlag::kHierarchyUsedThisFrameDescendant));

  EXPECT_EQ(parent_flags, propagated_hierarchy);
}

TEST(VirtualShadowFlagsContractsTest,
  StructuralFlagCoherenceIgnoresGpuOwnedUsedAndDetailBits)
{
  constexpr auto cpu_flags
    = MakeVirtualShadowPageFlags(true, true, false, false, false);
  constexpr auto gpu_flags
    = MakeVirtualShadowPageFlags(true, true, false, true, true);
  constexpr auto cpu_hierarchy
    = MergeVirtualShadowHierarchyFlags(0U, cpu_flags);
  constexpr auto gpu_hierarchy
    = MergeVirtualShadowHierarchyFlags(0U, gpu_flags);

  EXPECT_TRUE(VirtualShadowPageFlagsStructurallyEqual(cpu_flags, gpu_flags));
  EXPECT_EQ(NormalizeVirtualShadowPageFlagsForStructuralCoherence(cpu_flags),
    NormalizeVirtualShadowPageFlagsForStructuralCoherence(gpu_flags));
  EXPECT_TRUE(
    VirtualShadowPageFlagsStructurallyEqual(cpu_hierarchy, gpu_hierarchy));
  EXPECT_EQ(
    NormalizeVirtualShadowPageFlagsForStructuralCoherence(cpu_hierarchy),
    NormalizeVirtualShadowPageFlagsForStructuralCoherence(gpu_hierarchy));
}

TEST(VirtualShadowFlagsContractsTest,
  StructuralFlagCoherencePreservesResidencyAndInvalidationBits)
{
  constexpr auto allocated_clean
    = MakeVirtualShadowPageFlags(true, false, false, false, false);
  constexpr auto dynamic_uncached
    = MakeVirtualShadowPageFlags(true, true, false, false, false);
  constexpr auto static_uncached
    = MakeVirtualShadowPageFlags(true, false, true, false, false);

  EXPECT_FALSE(
    VirtualShadowPageFlagsStructurallyEqual(allocated_clean, dynamic_uncached));
  EXPECT_FALSE(
    VirtualShadowPageFlagsStructurallyEqual(allocated_clean, static_uncached));
  EXPECT_FALSE(
    VirtualShadowPageFlagsStructurallyEqual(dynamic_uncached, static_uncached));
}

TEST(VirtualShadowFlagsContractsTest,
  PageFlagsHierarchyVisibilityMatchesFallbackPolicy)
{
  EXPECT_FALSE(HasVirtualShadowHierarchyVisibility(0U));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(true, false, false, false, false)));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(false, false, false, true, false)));
  EXPECT_TRUE(HasVirtualShadowHierarchyVisibility(MergeVirtualShadowHierarchyFlags(
    0U, MakeVirtualShadowPageFlags(true, false, false, true, false))));
  EXPECT_FALSE(HasVirtualShadowHierarchyVisibility(
    MakeVirtualShadowPageFlags(false, false, true, false, false)));
}

} // namespace
