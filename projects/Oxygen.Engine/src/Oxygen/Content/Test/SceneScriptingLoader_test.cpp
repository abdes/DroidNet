//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

auto PackScriptingRecords(
  const std::initializer_list<oxygen::data::pak::ScriptingComponentRecord>
    records) -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(
    records.size() * sizeof(oxygen::data::pak::ScriptingComponentRecord));
  size_t i = 0;
  for (const auto& record : records) {
    auto slot
      = std::span<std::byte>(bytes).subspan(i * sizeof(record), sizeof(record));
    std::memcpy(slot.data(), &record, sizeof(record));
    ++i;
  }
  return bytes;
}

} // namespace

NOLINT_TEST(SceneScriptingLoaderTest, ValidateScriptingSlotRangesValid)
{
  const auto bytes = PackScriptingRecords({
    { .node_index = 0, .slot_start_index = 0, .slot_count = 2 },
    { .node_index = 1, .slot_start_index = 2, .slot_count = 1 },
  });

  EXPECT_NO_THROW({
    oxygen::content::loaders::detail::ValidateScriptingSlotRanges(bytes, 2, 3);
  });
}

NOLINT_TEST(SceneScriptingLoaderTest, ValidateScriptingSlotRangesOutOfBounds)
{
  const auto bytes = PackScriptingRecords(
    { { .node_index = 0, .slot_start_index = 3, .slot_count = 1 } });

  EXPECT_THROW(
    {
      oxygen::content::loaders::detail::ValidateScriptingSlotRanges(
        bytes, 1, 3);
    },
    std::runtime_error);
}

NOLINT_TEST(
  SceneScriptingLoaderTest, ValidateScriptingSlotRangesOverlapWarnsNoThrow)
{
  const auto bytes = PackScriptingRecords({
    { .node_index = 0, .slot_start_index = 1, .slot_count = 2 },
    { .node_index = 1, .slot_start_index = 2, .slot_count = 2 },
  });

  EXPECT_NO_THROW({
    oxygen::content::loaders::detail::ValidateScriptingSlotRanges(bytes, 2, 6);
  });
}

NOLINT_TEST(SceneScriptingLoaderTest, ValidateComponentTableRejectsBadSort)
{
  const auto bytes = PackScriptingRecords({
    { .node_index = 2, .slot_start_index = 0, .slot_count = 1 },
    { .node_index = 1, .slot_start_index = 1, .slot_count = 1 },
  });

  EXPECT_THROW(
    {
      oxygen::content::loaders::detail::ValidateComponentTable<
        oxygen::data::pak::ScriptingComponentRecord>(
        bytes, 2, sizeof(oxygen::data::pak::ScriptingComponentRecord), 3);
    },
    std::runtime_error);
}
