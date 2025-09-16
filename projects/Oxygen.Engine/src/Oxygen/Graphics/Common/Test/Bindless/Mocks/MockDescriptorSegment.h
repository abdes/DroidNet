//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <gmock/gmock.h>

#include <Oxygen/Graphics/Common/Detail/DescriptorSegment.h>

namespace oxygen::graphics::bindless::testing {

// ReSharper disable once CppClassCanBeFinal - Mock class cannot be final
// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
class MockDescriptorSegment : public detail::DescriptorSegment {
public:
  // clang-format off
  // NOLINTBEGIN
  MOCK_METHOD(oxygen::bindless::HeapIndex, Allocate, (), (override, noexcept));
  MOCK_METHOD(bool, Release, (oxygen::bindless::HeapIndex), (override, noexcept));
  MOCK_METHOD(oxygen::bindless::Count, GetAvailableCount, (), (const, override, noexcept));
  MOCK_METHOD(ResourceViewType, GetViewType, (), (const, override, noexcept));
  MOCK_METHOD(DescriptorVisibility, GetVisibility, (), (const, override, noexcept));
  MOCK_METHOD(oxygen::bindless::HeapIndex, GetBaseIndex, (), (const, override, noexcept));
  MOCK_METHOD(oxygen::bindless::Capacity, GetCapacity, (), (const, override, noexcept));
  MOCK_METHOD(oxygen::bindless::Count, GetAllocatedCount, (), (const, override, noexcept));
  // NOLINTEND
  // clang-format off
};

} // namespace oxygen::graphics::bindless::testing
