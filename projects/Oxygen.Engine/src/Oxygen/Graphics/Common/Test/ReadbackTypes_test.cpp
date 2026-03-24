//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>

namespace {

NOLINT_TEST(ReadbackTypesTest, EnumToString_ReturnsExpectedNames)
{
  const auto* error_name
    = nostd::to_string(oxygen::graphics::ReadbackError::kCancelled);
  const auto* state_name
    = nostd::to_string(oxygen::graphics::ReadbackState::kPending);
  const auto* mode_name
    = nostd::to_string(oxygen::graphics::MsaaReadbackMode::kResolveIfNeeded);

  ASSERT_NE(error_name, nullptr);
  ASSERT_NE(state_name, nullptr);
  ASSERT_NE(mode_name, nullptr);
  EXPECT_GT(std::char_traits<char>::length(error_name), 0U);
  EXPECT_GT(std::char_traits<char>::length(state_name), 0U);
  EXPECT_GT(std::char_traits<char>::length(mode_name), 0U);
}

NOLINT_TEST(ReadbackTypesTest, ErrorCategory_FormatsMessages)
{
  const auto error = oxygen::graphics::ReadbackError::kTicketNotFound;
  const auto error_code = oxygen::graphics::make_error_code(error);
  const auto message = error_code.message();

  EXPECT_FALSE(message.empty());
  EXPECT_EQ(
    error_code.category().name(), std::string_view { "Readback Error" });
}

NOLINT_TEST(ReadbackTypesTest, ReadbackPayloadTypes_DefaultConstruct)
{
  const oxygen::graphics::OwnedTextureReadbackData owned_data;
  const oxygen::graphics::ReadbackSurfaceMapping surface_mapping;

  EXPECT_TRUE(owned_data.bytes.empty());
  EXPECT_TRUE(owned_data.tightly_packed);
  EXPECT_EQ(surface_mapping.data, nullptr);
  EXPECT_EQ(surface_mapping.layout.width, 0U);
  EXPECT_EQ(surface_mapping.layout.height, 0U);
}

} // namespace
