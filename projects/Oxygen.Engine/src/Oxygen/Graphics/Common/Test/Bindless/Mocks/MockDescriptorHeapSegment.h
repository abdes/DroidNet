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
  // Updated to match DescriptorHeapSegment interface: [[nodiscard]] and
  // noexcept
  MOCK_METHOD(oxygen::bindless::Handle, Allocate, (), (override, noexcept));
  MOCK_METHOD(bool, Release, (oxygen::bindless::Handle), (override, noexcept));
  MOCK_METHOD(oxygen::bindless::Count, GetAvailableCount, (),
    (const, override, noexcept));
  MOCK_METHOD(ResourceViewType, GetViewType, (), (const, override, noexcept));
  MOCK_METHOD(
    DescriptorVisibility, GetVisibility, (), (const, override, noexcept));
  MOCK_METHOD(
    oxygen::bindless::Handle, GetBaseIndex, (), (const, override, noexcept));
  MOCK_METHOD(
    oxygen::bindless::Capacity, GetCapacity, (), (const, override, noexcept));
  MOCK_METHOD(oxygen::bindless::Count, GetAllocatedCount, (),
    (const, override, noexcept));
  MOCK_METHOD(oxygen::bindless::Handle, GetShaderVisibleIndex,
    (const DescriptorHandle&), (const, override, noexcept));
};

} // namespace oxygen::graphics::bindless::testing
