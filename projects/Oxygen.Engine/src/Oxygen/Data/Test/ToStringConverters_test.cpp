//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// GTest wrapper
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/BufferResource.h>

using oxygen::Format;
using oxygen::to_string; // Format overload (ADL + global)
using oxygen::data::BufferResource;
using oxygen::data::to_string; // BufferResource::UsageFlags overload

namespace {

//! Tests for to_string(BufferResource::UsageFlags) completeness and formatting.
class ToStringConvertersUsageFlagsTest : public testing::Test { };

NOLINT_TEST_F(
  ToStringConvertersUsageFlagsTest, BufferUsageFlags_AllFlagsPresent)
{
  // Arrange
  using UF = BufferResource::UsageFlags;
  // Combine all flags (excluding kNone)
  auto all_flags = static_cast<UF>(static_cast<uint32_t>(UF::kVertexBuffer)
    | static_cast<uint32_t>(UF::kIndexBuffer)
    | static_cast<uint32_t>(UF::kConstantBuffer)
    | static_cast<uint32_t>(UF::kStorageBuffer)
    | static_cast<uint32_t>(UF::kIndirectBuffer)
    | static_cast<uint32_t>(UF::kCPUWritable)
    | static_cast<uint32_t>(UF::kCPUReadable)
    | static_cast<uint32_t>(UF::kDynamic) | static_cast<uint32_t>(UF::kStatic)
    | static_cast<uint32_t>(UF::kImmutable));

  // Act
  auto s = to_string(all_flags);

  // Assert: each token appears exactly once separated by " | ".
  const char* tokens[] = { "VertexBuffer", "IndexBuffer", "ConstantBuffer",
    "StorageBuffer", "IndirectBuffer", "CPUWritable", "CPUReadable", "Dynamic",
    "Static", "Immutable" };

  for (auto* t : tokens) {
    auto first_pos = s.find(t);
    ASSERT_NE(first_pos, std::string::npos) << "Missing token: " << t;
    auto second_pos = s.find(t, first_pos + 1);
    EXPECT_EQ(second_pos, std::string::npos)
      << "Token appears more than once: " << t;
  }

  // Basic separator count check: tokens - 1 occurrences of " | ".
  size_t sep_count = 0;
  for (size_t pos = s.find(" | "); pos != std::string::npos;
    pos = s.find(" | ", pos + 3)) {
    ++sep_count;
  }
  EXPECT_EQ(sep_count, std::size(tokens) - 1);
}

//! Tests for to_string(Format) covering all known enumerators.
class ToStringConvertersFormatEnumTest : public testing::Test { };

NOLINT_TEST_F(
  ToStringConvertersFormatEnumTest, FormatEnum_AllKnownFormatsMapped)
{
  // Arrange / Act / Assert
  // Iterate all enum values linearly up to kMaxFormat and ensure mapping does
  // not return fallback string.
  for (uint32_t v = 0; v <= static_cast<uint32_t>(Format::kMaxFormat); ++v) {
    auto f = static_cast<Format>(v);
    auto name = to_string(f);
    EXPECT_STRNE(name, "__NotSupported__")
      << "Format value " << v << " missing to_string mapping";
  }
}

} // namespace
