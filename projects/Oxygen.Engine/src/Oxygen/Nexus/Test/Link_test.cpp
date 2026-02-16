//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <iostream>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/GenerationTracker.h>
#include <Oxygen/Nexus/Types/Domain.h>

//! Smoke test to verify Nexus module linking and symbol access.
/*!
 This test exercises key Nexus module components to ensure:
 1. The library links correctly without undefined symbols
 2. Core types and classes are accessible and functional
 3. Basic API contracts work as expected

 The test focuses on the most fundamental operations that would fail
 immediately if there were linking or ABI issues.
*/
auto main(int /*argc*/, char** /*argv*/) -> int
{
  using oxygen::VersionedBindlessHandle;
  using oxygen::bindless::Capacity;
  using oxygen::bindless::HeapIndex;
  using oxygen::graphics::DescriptorVisibility;
  using oxygen::graphics::ResourceViewType;
  using oxygen::nexus::DomainKey;

  // Constants for test values to avoid magic numbers
  constexpr Capacity kTrackerCapacity { 16U };
  constexpr HeapIndex kSampleIndex { 5U };
  constexpr HeapIndex kOutOfRangeIndex { 100U };
  constexpr HeapIndex kVersionedHandleIndex { 42U };
  constexpr VersionedBindlessHandle::Generation kSampleGeneration { 5U };
  constexpr VersionedBindlessHandle::Generation kGen1 { 1U };

  // Test 1: Verify DomainKey construction and comparison
  {
    std::cout << "Testing DomainKey construction and equality...\n";

    const DomainKey domain1 {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = DescriptorVisibility::kShaderVisible,
    };

    const DomainKey domain2 {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = DescriptorVisibility::kShaderVisible,
    };

    const DomainKey domain3 {
      .view_type = ResourceViewType::kTypedBuffer_SRV,
      .visibility = DescriptorVisibility::kShaderVisible,
    };

    if (domain1 == domain2 && !(domain1 == domain3)) {
      std::cout << "✓ DomainKey equality works correctly\n";
    } else {
      std::cout << "✗ DomainKey equality failed\n";
      return 1;
    }
  }
  std::cout << "\n";

  // Test 2: Verify GenerationTracker basic functionality
  {
    std::cout << "Testing GenerationTracker basic operations...\n";

    oxygen::nexus::GenerationTracker tracker(kTrackerCapacity);
    const auto handle = kSampleIndex;

    // Test lazy initialization
    const auto gen1 = tracker.Load(handle);
    if (gen1.get() >= kGen1.get()) {
      std::cout << "✓ GenerationTracker lazy initialization works (gen="
                << gen1.get() << ")\n";
    } else {
      std::cout << "✗ GenerationTracker lazy initialization failed\n";
      return 1;
    }
    std::cout << "\n";

    // Test generation bump
    tracker.Bump(handle);
    const auto gen2 = tracker.Load(handle);
    if (gen2.get() == gen1.get() + kGen1.get()) {
      std::cout << "✓ GenerationTracker bump increments correctly (gen="
                << gen2.get() << ")\n";
    } else {
      std::cout << "✗ GenerationTracker bump failed\n";
      return 1;
    }

    // Test out-of-bounds safety
    const auto gen_oob = tracker.Load(kOutOfRangeIndex);
    if (gen_oob.get() == 0U) {
      std::cout << "✓ GenerationTracker out-of-bounds returns 0\n";
    } else {
      std::cout << "✗ GenerationTracker out-of-bounds check failed\n";
      return 1;
    }
  }
  std::cout << "\n";

  // Test 3: Verify VersionedBindlessHandle basic operations
  {
    std::cout
      << "Testing VersionedBindlessHandle construction and validation...\n";

    const auto handle = kVersionedHandleIndex;
    const auto generation = kSampleGeneration;

    const VersionedBindlessHandle versioned_handle { handle, generation };

    if (versioned_handle.IsValid()
      && versioned_handle.ToBindlessHandle() == handle
      && versioned_handle.GenerationValue() == generation) {
      std::cout
        << "✓ VersionedBindlessHandle construction and accessors work\n";
    } else {
      std::cout << "✗ VersionedBindlessHandle operations failed\n";
      return 1;
    }

    // Test invalid handle
    const VersionedBindlessHandle invalid_handle {};
    if (!invalid_handle.IsValid()) {
      std::cout << "✓ Default VersionedBindlessHandle is invalid as expected\n";
    } else {
      std::cout << "✗ Default VersionedBindlessHandle should be invalid\n";
      return 1;
    }
  }
  std::cout << "\n";

  std::cout << "The module links correctly and core APIs are functional.\n";

  return 0;
}
