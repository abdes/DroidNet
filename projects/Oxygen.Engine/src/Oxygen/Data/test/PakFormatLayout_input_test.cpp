//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace input = oxygen::data::pak::input;

using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::Key;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::WireBytes;

//! A data table is its offset, entry count and entry size.
NOLINT_TEST(PakFormatLayoutInputTest, InputDataTableWireBytes)
{
  const auto table = input::InputDataTable {
    .offset = 0x0102030405060708U,
    .count = 0x11121314U,
    .entry_size = 0x21222324U,
  };

  const auto expected
    = WireBytes {}.U64(0x0102030405060708U).U32(0x11121314U).U32(0x21222324U);

  EXPECT_EQ(Encoded(table), expected.Bytes());
}

//! An input action is its header, value type and flags.
NOLINT_TEST(PakFormatLayoutInputTest, InputActionAssetDescWireBytes)
{
  const auto action = input::InputActionAssetDesc {
    .header = MakeHeader(),
    .value_type = input::InputActionValueType::kAxis2D,
    .flags = input::InputActionAssetFlags::kConsumesInput
      | input::InputActionAssetFlags::kCumulativeAccumulation,
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes()).U8(0x02U).U32(0x03U);

  EXPECT_EQ(Encoded(action), expected.Bytes());
}

//! A mapping context is its header, flags, default priority and the mapping,
//! trigger and trigger auxiliary tables.
NOLINT_TEST(PakFormatLayoutInputTest, InputMappingContextAssetDescWireBytes)
{
  const auto context = input::InputMappingContextAssetDesc {
    .header = MakeHeader(),
    .flags = static_cast<input::InputMappingContextFlags>(0x81828384U),
    .default_priority = 0x05060708,
    .mappings = { .offset = 0x1112131415161718U,
      .count = 0x21222324U,
      .entry_size = 0x31323334U, },
    .triggers = { .offset = 0x4142434445464748U,
      .count = 0x51525354U,
      .entry_size = 0x61626364U, },
    .trigger_aux = { .offset = 0x7172737475767778U,
      .count = 0x81828384U,
      .entry_size = 0x91929394U, },
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .U32(0x81828384U)
    .U32(0x05060708U)
    .U64(0x1112131415161718U)
    .U32(0x21222324U)
    .U32(0x31323334U)
    .U64(0x4142434445464748U)
    .U32(0x51525354U)
    .U32(0x61626364U)
    .U64(0x7172737475767778U)
    .U32(0x81828384U)
    .U32(0x91929394U);

  EXPECT_EQ(Encoded(context), expected.Bytes());
}

//! A mapping is its action key, slot name, trigger range, flags, scale and
//! bias.
NOLINT_TEST(PakFormatLayoutInputTest, InputActionMappingRecordWireBytes)
{
  const auto mapping = input::InputActionMappingRecord {
    .action_asset_key = Key(0x10U),
    .slot_name_offset = 0x21222324U,
    .trigger_start_index = 0x31323334U,
    .trigger_count = 0x41424344U,
    .flags = static_cast<input::InputMappingFlags>(0x51525354U),
    .scale = { 1.5F, 2.5F, },
    .bias = { 3.5F, 4.5F, },
  };

  const auto expected = WireBytes {}
                          .Run(0x10U, 16)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .Floats(1.5F, 2)
                          .Floats(3.5F, 2);

  EXPECT_EQ(Encoded(mapping), expected.Bytes());
}

//! A trigger is its type, behavior, flags, actuation threshold, linked action
//! key, auxiliary range, five float and five integer parameters.
NOLINT_TEST(PakFormatLayoutInputTest, InputTriggerRecordWireBytes)
{
  const auto trigger = input::InputTriggerRecord {
    .type = input::InputTriggerType::kChord,
    .behavior = input::InputTriggerBehavior::kBlocker,
    .flags = 0x11121314U,
    .actuation_threshold = 1.5F,
    .linked_action_asset_key = Key(0x20U),
    .aux_start_index = 0x31323334U,
    .aux_count = 0x41424344U,
    .fparams = { 2.5F, 3.5F, 4.5F, 5.5F, 6.5F, },
    .uparams = { 0x51U, 0x52U, 0x53U, 0x54U, 0x55U, },
  };

  const auto expected = WireBytes {}
                          .U8(7U)
                          .U8(2U)
                          .U32(0x11121314U)
                          .F32(1.5F)
                          .Run(0x20U, 16)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .Floats(2.5F, 5)
                          .U32(0x51U)
                          .U32(0x52U)
                          .U32(0x53U)
                          .U32(0x54U)
                          .U32(0x55U);

  EXPECT_EQ(Encoded(trigger), expected.Bytes());
}

//! An auxiliary trigger record is its action key, completion states, time to
//! complete in nanoseconds and flags.
NOLINT_TEST(PakFormatLayoutInputTest, InputTriggerAuxRecordWireBytes)
{
  const auto aux = input::InputTriggerAuxRecord {
    .action_asset_key = Key(0x10U),
    .completion_states = 0x21222324U,
    .time_to_complete_ns = 0x3132333435363738U,
    .flags = 0x41424344U,
  };

  const auto expected = WireBytes {}
                          .Run(0x10U, 16)
                          .U32(0x21222324U)
                          .U64(0x3132333435363738U)
                          .U32(0x41424344U);

  EXPECT_EQ(Encoded(aux), expected.Bytes());
}

} // namespace
