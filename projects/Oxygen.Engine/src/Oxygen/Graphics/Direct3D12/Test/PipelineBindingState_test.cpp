//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Direct3D12/Detail/PipelineBindingState.h>
#include <Oxygen/Graphics/Direct3D12/Test/Mocks/MockDescriptorHeap.h>
#include <Oxygen/Graphics/Direct3D12/Test/Mocks/MockRootSignature.h>

namespace {

using oxygen::graphics::d3d12::detail::PipelineBindingState;
using oxygen::graphics::d3d12::testing::MockDescriptorHeap;
using oxygen::graphics::d3d12::testing::MockRootSignature;

//! Repeated pipeline binding preserves both graphics and compute arguments.
NOLINT_TEST(PipelineBindingStateTest, RepeatedBindingsNeedNoNativeCommands)
{
  // Arrange
  PipelineBindingState state;
  MockDescriptorHeap heap;
  MockRootSignature graphics_signature;
  MockRootSignature compute_signature;
  ASSERT_TRUE(state.ChangeHeaps({ &heap, nullptr }));
  ASSERT_TRUE(state.ChangeRootSignature(&graphics_signature, false));
  ASSERT_TRUE(state.ChangeRootSignature(&compute_signature, true));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, true));

  // Act / Assert
  EXPECT_FALSE(state.ChangeHeaps({ &heap, nullptr }));
  EXPECT_FALSE(state.ChangeRootSignature(&compute_signature, true));
  EXPECT_FALSE(state.ChangeRootSignature(&graphics_signature, false));
  EXPECT_FALSE(state.ChangeDescriptorTable(0, 100, true));
  EXPECT_FALSE(state.ChangeDescriptorTable(0, 100, false));
}

//! Changing a root signature invalidates only that pipeline domain's tables.
NOLINT_TEST(PipelineBindingStateTest, RootSignatureChangePreservesOtherDomain)
{
  // Arrange
  PipelineBindingState state;
  MockRootSignature original;
  MockRootSignature replacement;
  ASSERT_TRUE(state.ChangeRootSignature(&original, false));
  ASSERT_TRUE(state.ChangeRootSignature(&original, true));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, true));

  // Act
  const bool changed = state.ChangeRootSignature(&replacement, true);

  // Assert
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.RootSignature(false), &original);
  EXPECT_EQ(state.RootSignature(true), &replacement);
  EXPECT_FALSE(state.ChangeDescriptorTable(0, 100, false));
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 100, true));
}

//! Directly indexed heap changes require new root bindings in both domains.
NOLINT_TEST(PipelineBindingStateTest, HeapChangeInvalidatesRootsAndTables)
{
  // Arrange
  PipelineBindingState state;
  MockDescriptorHeap original;
  MockDescriptorHeap replacement;
  MockRootSignature signature;
  ASSERT_TRUE(state.ChangeHeaps({ &original, nullptr }));
  ASSERT_TRUE(state.ChangeRootSignature(&signature, false));
  ASSERT_TRUE(state.ChangeRootSignature(&signature, true));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, true));

  // Act
  const bool changed = state.ChangeHeaps({ &replacement, nullptr });

  // Assert
  EXPECT_TRUE(changed);
  EXPECT_EQ(state.RootSignature(false), nullptr);
  EXPECT_EQ(state.RootSignature(true), nullptr);
  EXPECT_TRUE(state.ChangeRootSignature(&signature, false));
  EXPECT_TRUE(state.ChangeRootSignature(&signature, true));
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 100, true));
}

//! A new GPU address in the same heap still requires a table command.
NOLINT_TEST(PipelineBindingStateTest, TableAddressesChangeIndependently)
{
  // Arrange
  PipelineBindingState state;
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, true));
  ASSERT_TRUE(state.ChangeDescriptorTable(1, 200, true));

  // Act / Assert
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 300, true));
  EXPECT_FALSE(state.ChangeDescriptorTable(1, 200, true));
  EXPECT_FALSE(state.ChangeDescriptorTable(0, 300, true));
}

//! Reusing a command list must replay all initial binding commands.
NOLINT_TEST(PipelineBindingStateTest, RecordingResetInvalidatesAllBindings)
{
  // Arrange
  PipelineBindingState state;
  MockDescriptorHeap heap;
  MockRootSignature signature;
  ASSERT_TRUE(state.ChangeHeaps({ &heap, nullptr }));
  ASSERT_TRUE(state.ChangeRootSignature(&signature, false));
  ASSERT_TRUE(state.ChangeRootSignature(&signature, true));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  ASSERT_TRUE(state.ChangeDescriptorTable(0, 100, true));

  // Act
  state.Reset();

  // Assert
  EXPECT_EQ(state.RootSignature(false), nullptr);
  EXPECT_EQ(state.RootSignature(true), nullptr);
  EXPECT_TRUE(state.ChangeHeaps({ &heap, nullptr }));
  EXPECT_TRUE(state.ChangeRootSignature(&signature, false));
  EXPECT_TRUE(state.ChangeRootSignature(&signature, true));
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 100, false));
  EXPECT_TRUE(state.ChangeDescriptorTable(0, 100, true));
}

} // namespace
