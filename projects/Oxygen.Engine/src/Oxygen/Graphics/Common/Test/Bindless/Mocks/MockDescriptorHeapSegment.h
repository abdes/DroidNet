//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file MockDescriptorHeapSegment.h
 *
 * Mock implementation of a descriptor heap segment for testing purposes.
 */

#pragma once

#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <gmock/gmock.h>

namespace oxygen::graphics::testing {

class MockDescriptorHeapSegment : public detail::DescriptorHeapSegment {
public:
    MOCK_METHOD(uint32_t, Allocate, (), (override));
    MOCK_METHOD(bool, Release, (uint32_t), (override));
    MOCK_METHOD(uint32_t, GetAvailableCount, (), (const, override));
    MOCK_METHOD(ResourceViewType, GetViewType, (), (const, override));
    MOCK_METHOD(DescriptorVisibility, GetVisibility, (), (const, override));
    MOCK_METHOD(uint32_t, GetBaseIndex, (), (const, override));
    MOCK_METHOD(uint32_t, GetCapacity, (), (const, override));
    MOCK_METHOD(uint32_t, GetSize, (), (const, override));
};

} // namespace oxygen::graphics::testing
