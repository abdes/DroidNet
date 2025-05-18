//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <gmock/gmock.h>

#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
class MockDescriptorHeapSegment : public detail::DescriptorHeapSegment {
public:
    // Updated to match DescriptorHeapSegment interface: [[nodiscard]] and noexcept
    MOCK_METHOD(IndexT, Allocate, (), (override, noexcept));
    MOCK_METHOD(bool, Release, (IndexT), (override, noexcept));
    MOCK_METHOD(IndexT, GetAvailableCount, (), (const, override, noexcept));
    MOCK_METHOD(ResourceViewType, GetViewType, (), (const, override, noexcept));
    MOCK_METHOD(DescriptorVisibility, GetVisibility, (), (const, override, noexcept));
    MOCK_METHOD(IndexT, GetBaseIndex, (), (const, override, noexcept));
    MOCK_METHOD(IndexT, GetCapacity, (), (const, override, noexcept));
    MOCK_METHOD(IndexT, GetAllocatedCount, (), (const, override, noexcept));
};

} // namespace oxygen::graphics::bindless::testing
