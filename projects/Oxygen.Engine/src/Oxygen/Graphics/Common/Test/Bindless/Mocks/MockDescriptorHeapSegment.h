//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Detail/DescriptorHeapSegment.h>
#include <gmock/gmock.h>

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
class MockDescriptorHeapSegment : public detail::DescriptorHeapSegment {
public:
    // Updated to match DescriptorHeapSegment interface: [[nodiscard]] and noexcept
    MOCK_METHOD(uint32_t, Allocate, (), (override, noexcept));
    MOCK_METHOD(bool, Release, (uint32_t), (override, noexcept));
    MOCK_METHOD(uint32_t, GetAvailableCount, (), (const, override, noexcept));
    MOCK_METHOD(ResourceViewType, GetViewType, (), (const, override, noexcept));
    MOCK_METHOD(DescriptorVisibility, GetVisibility, (), (const, override, noexcept));
    MOCK_METHOD(uint32_t, GetBaseIndex, (), (const, override, noexcept));
    MOCK_METHOD(uint32_t, GetCapacity, (), (const, override, noexcept));
    MOCK_METHOD(uint32_t, GetSize, (), (const, override, noexcept));
};

} // namespace oxygen::graphics::bindless::testing
