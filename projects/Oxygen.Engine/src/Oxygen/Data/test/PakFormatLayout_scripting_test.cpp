//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace scripting = oxygen::data::pak::scripting;
namespace world = oxygen::data::pak::world;

using oxygen::data::AssetType;
using oxygen::data::ResourceReferenceIndex;
using oxygen::data::ScriptSlotId;

using oxygen::data::pak::testing::Digest;
using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::IdBytes;
using oxygen::data::pak::testing::Key;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::RawBytes;
using oxygen::data::pak::testing::WireBytes;
using oxygen::data::pak::testing::ZeroedRecord;

//! A script slot id whose bytes count up from `first`.
auto Slot(const uint8_t first) -> ScriptSlotId
{
  return ScriptSlotId::FromBytes(IdBytes(first));
}

//! A zeroed parameter of `type`, named by the string offset 0x11121314.
auto MakeParam(const scripting::ScriptParamType type)
  -> scripting::ScriptParamRecord
{
  auto record = ZeroedRecord<scripting::ScriptParamRecord>();
  record.key_offset = 0x11121314U;
  record.type = type;
  return record;
}

//! The documented start of a parameter: the key offset 0x11121314 and `type`.
auto ParamPrefix(const uint32_t type) -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U32(0x11121314U).U32(type);
  return bytes;
}

//! An asset parameter value is its key and the declared asset type. It is not
//! a Serio record, so its in-memory packed bytes are compared.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptAssetParamValueWireBytes)
{
  const auto value = scripting::ScriptAssetParamValue {
    .key = Key(0x10U),
    .asset_type = static_cast<AssetType>(0x03U),
  };

  const auto expected = WireBytes {}.Run(0x10U, 16).U8(0x03U);

  EXPECT_EQ(RawBytes(value), expected.Bytes());
}

//! A bool parameter is its key offset, type, and the bool arm, zero-padded to
//! the 17 bytes of the widest arm. The parameter is not a Serio record, so its
//! in-memory packed bytes are compared.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordBoolArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kBool);
  record.value.as_bool = 1U;

  auto expected = ParamPrefix(1U);
  expected.U8(1U).Zeros(16);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! An int32 parameter is its key offset, type, and the int32 arm, zero-padded
//! to 17 bytes. It is compared by its in-memory packed bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordInt32ArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kInt32);
  record.value.as_int32 = 0x21222324;

  auto expected = ParamPrefix(2U);
  expected.U32(0x21222324U).Zeros(13);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A float parameter is its key offset, type, and the float arm, zero-padded
//! to 17 bytes. It is compared by its in-memory packed bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordFloatArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kFloat);
  record.value.as_float = 1.5F;

  auto expected = ParamPrefix(3U);
  expected.F32(1.5F).Zeros(13);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A string parameter is its key offset, type, and the string offset arm,
//! zero-padded to 17 bytes. It is compared by its in-memory packed bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordStringArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kString);
  record.value.as_string_offset = 0x31323334U;

  auto expected = ParamPrefix(4U);
  expected.U32(0x31323334U).Zeros(13);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A vector parameter is its key offset, type, and the four-float arm,
//! zero-padded to 17 bytes. It is compared by its in-memory packed bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordVectorArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kVec4);
  record.value.as_vec[0] = 2.5F;
  record.value.as_vec[1] = 3.5F;
  record.value.as_vec[2] = 4.5F;
  record.value.as_vec[3] = 5.5F;

  auto expected = ParamPrefix(7U);
  expected.Floats(2.5F, 4).Zeros(1);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! An asset parameter is its key offset, type, and the asset arm: key and
//! asset type. It is compared by its in-memory packed bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordAssetArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kAsset);
  record.value.as_asset = scripting::ScriptAssetParamValue {
    .key = Key(0x40U),
    .asset_type = static_cast<AssetType>(0x03U),
  };

  auto expected = ParamPrefix(8U);
  expected.Run(0x40U, 16).U8(0x03U);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A node parameter is its key offset, type, and the node arm: node index and
//! node path, zero-padded to 17 bytes. It is compared by its in-memory packed
//! bytes.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamRecordNodeArmWireBytes)
{
  auto record = MakeParam(scripting::ScriptParamType::kNode);
  record.value.as_node = world::NodeRef {
    .node_index = 0x51525354U,
    .node_path = 0x61626364U,
  };

  auto expected = ParamPrefix(9U);
  expected.U32(0x51525354U).U32(0x61626364U).Zeros(9);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A script slot is its slot id, script asset key, parameter array offset and
//! count, execution order and flags.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptSlotRecordWireBytes)
{
  const auto slot = scripting::ScriptSlotRecord {
    .slot_id = Slot(0x10U),
    .script_asset_key = Key(0x20U),
    .params_array_offset = 0x3132333435363738U,
    .params_count = 0x41424344U,
    .execution_order = 0x51525354,
    .flags = static_cast<scripting::ScriptSlotFlags>(0x61626364U),
  };

  const auto expected = WireBytes {}
                          .Run(0x10U, 16)
                          .Run(0x20U, 16)
                          .U64(0x3132333435363738U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .U32(0x61626364U);

  EXPECT_EQ(Encoded(slot), expected.Bytes());
}

//! A parameter override is its target node reference, slot id and the
//! parameter record. It is not a Serio record, so its in-memory packed bytes
//! are compared.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptParamOverrideRecordWireBytes)
{
  auto record = ZeroedRecord<scripting::ScriptParamOverrideRecord>();
  record.target = world::NodeRef {
    .node_index = 0x11121314U,
    .node_path = 0x21222324U,
  };
  record.slot_id = Slot(0x30U);
  record.param.key_offset = 0x41424344U;
  record.param.type = scripting::ScriptParamType::kInt32;
  record.param.value.as_int32 = 0x51525354;

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .Run(0x30U, 16)
                          .U32(0x41424344U)
                          .U32(2U)
                          .U32(0x51525354U)
                          .Zeros(13);

  EXPECT_EQ(RawBytes(record), expected.Bytes());
}

//! A scripting component is its node index, flags and slot range.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptingComponentRecordWireBytes)
{
  const auto component = scripting::ScriptingComponentRecord {
    .node_index = 0x11121314U,
    .flags = static_cast<scripting::ScriptingComponentFlags>(0x21222324U),
    .slot_start_index = 0x31323334U,
    .slot_count = 0x41424344U,
  };

  const auto expected = WireBytes {}
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U);

  EXPECT_EQ(Encoded(component), expected.Bytes());
}

//! A script resource row is its payload location, language, encoding,
//! compression and digest.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptResourceDescWireBytes)
{
  const auto row = scripting::ScriptResourceDesc {
    .data_offset = 0x0102030405060708U,
    .size_bytes = 0x11121314U,
    .language = scripting::ScriptLanguage::kLuau,
    .encoding = scripting::ScriptEncoding::kSource,
    .compression = scripting::ScriptCompression::kZstd,
    .content_hash = Digest(0x30U),
  };

  const auto expected = WireBytes {}
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U8(0U)
                          .U8(1U)
                          .U8(1U)
                          .Run(0x30U, 32);

  EXPECT_EQ(Encoded(row), expected.Bytes());
}

//! A script asset is its header, bytecode and source references, flags,
//! external source path offset and parameter table.
NOLINT_TEST(PakFormatLayoutScriptingTest, ScriptAssetDescWireBytes)
{
  const auto desc = scripting::ScriptAssetDesc {
    .header = MakeHeader(),
    .bytecode_resource_index = ResourceReferenceIndex { 0x81828384U },
    .source_resource_index = ResourceReferenceIndex { 0x91929394U },
    .flags = scripting::ScriptAssetFlags::kAllowExternalSource,
    .external_source_path_offset = 0xA1A2A3A4U,
    .params = { .offset = 0x0102030405060708U,
      .count = 0xB1B2B3B4U,
      .entry_size = 0xC1C2C3C4U, },
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .U32(0x81828384U)
    .U32(0x91929394U)
    .U32(1U)
    .U32(0xA1A2A3A4U)
    .U64(0x0102030405060708U)
    .U32(0xB1B2B3B4U)
    .U32(0xC1C2C3C4U);

  EXPECT_EQ(Encoded(desc), expected.Bytes());
}

} // namespace
