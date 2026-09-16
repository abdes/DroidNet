//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <string_view>

#include <Oxygen/Data/MaterialSlotInventory.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::data;
using ErrorCode = MaterialSlotValidationErrorCode;

auto GeometryKey() -> AssetKey
{
  return AssetKey::FromString("cccccccc-dddd-eeee-ffff-000000000001").value();
}

auto MaterialA() -> AssetKey
{
  return AssetKey::FromString("10213243-5465-7687-98a9-bacbdcedfe0f").value();
}

auto MaterialB() -> AssetKey
{
  return AssetKey::FromString("01234567-89ab-cdef-0123-456789abcdef").value();
}

auto GeometryBindings() -> std::array<MaterialSlotBinding, 4>
{
  return { MaterialSlotBinding { 0, 0, MaterialA() },
    MaterialSlotBinding { 0, 1, {} }, MaterialSlotBinding { 0, 2, MaterialA() },
    MaterialSlotBinding { 1, 0, MaterialB() } };
}

auto MakeInventory() -> MaterialSlotInventory
{
  MaterialSlotInventory inventory {
    .geometry_asset_key = GeometryKey(),
    .slots = {
      {
        .slot_id = MaterialSlotId::FromString(
          "ffeeddcc-bbaa-9988-7766-554433221100").value(),
        .display_name = "Surface",
        .bindings = { { 1, 0, MaterialB() }, { 0, 2, MaterialA() } },
      },
      {
        .slot_id = MaterialSlotId::FromString(
          "00112233-4455-6677-8899-aabbccddeeff").value(),
        .display_name = "Surface",
        .bindings = { { 0, 1, {} }, { 0, 0, MaterialA() } },
      },
    },
  };
  inventory.layout_revision
    = ComputeMaterialSlotLayoutRevision(inventory.slots).value();
  return inventory;
}

auto DigestText(const oxygen::base::Sha256Digest& digest) -> std::string
{
  std::string result;
  for (const auto byte : digest) {
    fmt::format_to(std::back_inserter(result), "{:02x}", byte);
  }
  return result;
}

NOLINT_TEST(MaterialSlotInventoryTest, MatchesIndependentCanonicalSha256Vector)
{
  const auto inventory = MakeInventory();
  const auto canonical = WriteCanonicalMaterialSlotLayout(inventory.slots);
  ASSERT_TRUE(canonical.has_value());
  constexpr std::string_view kExpected
    = "{\"schema_version\":1,\"slots\":["
      "{\"slot_id\":\"00112233-4455-6677-8899-aabbccddeeff\",\"bindings\":["
      "{\"lod_index\":0,\"submesh_index\":0,\"default_material_key\":"
      "\"10213243-5465-7687-98a9-bacbdcedfe0f\"},"
      "{\"lod_index\":0,\"submesh_index\":1,\"default_material_key\":"
      "\"00000000-0000-0000-0000-000000000000\"}]},"
      "{\"slot_id\":\"ffeeddcc-bbaa-9988-7766-554433221100\",\"bindings\":["
      "{\"lod_index\":0,\"submesh_index\":2,\"default_material_key\":"
      "\"10213243-5465-7687-98a9-bacbdcedfe0f\"},"
      "{\"lod_index\":1,\"submesh_index\":0,\"default_material_key\":"
      "\"01234567-89ab-cdef-0123-456789abcdef\"}]}]}";
  EXPECT_EQ(canonical.value(), kExpected);
  EXPECT_EQ(canonical.value().size(), 542U);
  // Independent Python hashlib.sha256 over the literal 542 UTF-8 bytes above.
  EXPECT_EQ(DigestText(inventory.layout_revision),
    "0431ab847da8506244f75ab18a00f5882a8b8180caeaa95668a7c4b9896a1461");
  EXPECT_TRUE(
    ValidateMaterialSlotInventory(inventory, GeometryKey(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(MaterialSlotInventoryTest,
  PermutationsAndLabelsDoNotChangeRevisionOrMutateDisplayOrder)
{
  const auto original = MakeInventory();
  for (const auto reverse_slots : { false, true }) {
    for (const auto reverse_first : { false, true }) {
      for (const auto reverse_second : { false, true }) {
        auto inventory = original;
        if (reverse_first) {
          std::ranges::reverse(inventory.slots.front().bindings);
        }
        if (reverse_second) {
          std::ranges::reverse(inventory.slots.back().bindings);
        }
        if (reverse_slots) {
          std::ranges::reverse(inventory.slots);
        }
        inventory.slots.front().display_name = "Renamed \"label\"\n";
        inventory.slots.back().display_name.clear();
        const auto first_id = inventory.slots.front().slot_id;
        const auto first_bindings = inventory.slots.front().bindings;
        const auto revision
          = ComputeMaterialSlotLayoutRevision(inventory.slots);
        ASSERT_TRUE(revision.has_value());
        EXPECT_EQ(revision.value(), original.layout_revision);
        EXPECT_EQ(inventory.slots.front().slot_id, first_id);
        EXPECT_EQ(inventory.slots.front().bindings, first_bindings);
        EXPECT_TRUE(ValidateMaterialSlotInventory(
          inventory, GeometryKey(), GeometryBindings())
            .has_value());
      }
    }
  }
}

NOLINT_TEST(MaterialSlotInventoryTest, EveryLayoutIdentityFieldAffectsRevision)
{
  struct Mutation {
    std::string_view name;
    void (*apply)(MaterialSlotInventory&);
  };
  const std::array cases {
    Mutation { "default key",
      [](auto& value) {
        value.slots.front().bindings.front().default_material_key = MaterialA();
      } },
    Mutation { "LOD",
      [](auto& value) { value.slots.front().bindings.front().lod_index = 2; } },
    Mutation { "submesh",
      [](auto& value) {
        value.slots.front().bindings.back().submesh_index = 3;
      } },
    Mutation { "slot ID",
      [](auto& value) {
        value.slots.front().slot_id
          = MaterialSlotId::FromString("11223344-5566-7788-99aa-bbccddeeff00")
              .value();
      } },
    Mutation { "surface owner",
      [](auto& value) {
        value.slots.back().bindings.push_back(
          value.slots.front().bindings.back());
        value.slots.front().bindings.pop_back();
      } },
  };
  for (const auto& mutation : cases) {
    SCOPED_TRACE(mutation.name);
    auto inventory = MakeInventory();
    mutation.apply(inventory);
    const auto changed = ComputeMaterialSlotLayoutRevision(inventory.slots);
    ASSERT_TRUE(changed.has_value());
    EXPECT_NE(changed.value(), inventory.layout_revision);
  }
}

NOLINT_TEST(MaterialSlotInventoryTest,
  DuplicateLabelsAndDefaultsRemainSeparateAndPerBindingDefaultsMayDiffer)
{
  const auto inventory = MakeInventory();
  ASSERT_EQ(inventory.slots.size(), 2U);
  EXPECT_EQ(
    inventory.slots.front().display_name, inventory.slots.back().display_name);
  EXPECT_NE(inventory.slots.front().slot_id, inventory.slots.back().slot_id);
  EXPECT_NE(inventory.slots.front().bindings.front().default_material_key,
    inventory.slots.front().bindings.back().default_material_key);
  EXPECT_EQ(inventory.slots.front().bindings.back().default_material_key,
    inventory.slots.back().bindings.back().default_material_key);
  EXPECT_TRUE(
    inventory.slots.back().bindings.front().default_material_key.IsNil());
  EXPECT_TRUE(
    ValidateMaterialSlotInventory(inventory, GeometryKey(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(MaterialSlotInventoryTest, RejectsInvalidSlotOwnershipBeforeHashing)
{
  struct InvalidCase {
    std::string_view name;
    void (*apply)(MaterialSlotInventory&);
    ErrorCode expected;
  };
  const std::array cases {
    InvalidCase { "nil ID",
      [](auto& value) { value.slots.front().slot_id = {}; },
      ErrorCode::kNilSlotId },
    InvalidCase { "duplicate ID",
      [](auto& value) {
        value.slots.front().slot_id = value.slots.back().slot_id;
      },
      ErrorCode::kDuplicateSlotId },
    InvalidCase { "unbound slot",
      [](auto& value) { value.slots.front().bindings.clear(); },
      ErrorCode::kEmptySlotBindings },
    InvalidCase { "duplicate within slot",
      [](auto& value) {
        value.slots.front().bindings.push_back(
          value.slots.front().bindings.front());
      },
      ErrorCode::kDuplicateBinding },
    InvalidCase { "duplicate across slots",
      [](auto& value) {
        value.slots.front().bindings.push_back(
          value.slots.back().bindings.front());
      },
      ErrorCode::kDuplicateBinding },
  };
  for (const auto& invalid : cases) {
    SCOPED_TRACE(invalid.name);
    auto inventory = MakeInventory();
    invalid.apply(inventory);
    const auto canonical = WriteCanonicalMaterialSlotLayout(inventory.slots);
    ASSERT_FALSE(canonical.has_value());
    EXPECT_EQ(canonical.error().code, invalid.expected);
    const auto revision = ComputeMaterialSlotLayoutRevision(inventory.slots);
    ASSERT_FALSE(revision.has_value());
    EXPECT_EQ(revision.error().code, invalid.expected);
    const auto validation = ValidateMaterialSlotInventory(
      inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(validation.has_value());
    EXPECT_EQ(validation.error().code, invalid.expected);
  }
}

NOLINT_TEST(MaterialSlotInventoryTest, RequiresExactGeometryIdentityAndSchema)
{
  auto inventory = MakeInventory();
  for (const auto version : { 0U, 2U, std::numeric_limits<uint32_t>::max() }) {
    inventory.schema_version = version;
    const auto result = ValidateMaterialSlotInventory(
      inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kUnsupportedSchemaVersion);
  }
  inventory.schema_version = kMaterialSlotInventorySchemaVersion;
  auto result
    = ValidateMaterialSlotInventory(inventory, {}, GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kNilGeometryKey);
  inventory.geometry_asset_key = {};
  result = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kNilGeometryKey);
  inventory.geometry_asset_key = MaterialA();
  result = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kGeometryIdentityMismatch);
  // Geometry scope is validated separately; it is not part of the layout hash.
  EXPECT_TRUE(
    ValidateMaterialSlotInventory(inventory, MaterialA(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(MaterialSlotInventoryTest, RejectsMissingAndOutOfRangeSurfaces)
{
  auto inventory = MakeInventory();
  inventory.slots.front().bindings.pop_back();
  auto result = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kMissingBinding);
  ASSERT_TRUE(result.error().binding.has_value());
  EXPECT_EQ(
    result.error().binding.value(), (MaterialSlotBindingLocation { 0, 2 }));
  for (const auto bad_lod : { false, true }) {
    inventory = MakeInventory();
    auto& binding = inventory.slots.front().bindings.front();
    if (bad_lod) {
      binding.lod_index = 2;
    } else {
      binding.submesh_index = 1;
    }
    result = ValidateMaterialSlotInventory(
      inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kUnknownBinding);
    EXPECT_EQ(result.error().slot_id, inventory.slots.front().slot_id);
    EXPECT_EQ(result.error().binding,
      (MaterialSlotBindingLocation {
        binding.lod_index, binding.submesh_index }));
  }
}

NOLINT_TEST(MaterialSlotInventoryTest, RejectsAmbiguousGeometryAndWrongDefaults)
{
  auto inventory = MakeInventory();
  auto bindings = GeometryBindings();
  bindings.back() = bindings.front();
  auto result
    = ValidateMaterialSlotInventory(inventory, GeometryKey(), bindings);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kDuplicateGeometryBinding);
  EXPECT_EQ(result.error().binding, (MaterialSlotBindingLocation { 0, 0 }));

  inventory.slots.front().bindings.front().default_material_key = {};
  result = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kDefaultMaterialMismatch);
  EXPECT_EQ(result.error().binding, (MaterialSlotBindingLocation { 1, 0 }));
}

NOLINT_TEST(MaterialSlotInventoryTest, RejectsStaleRecordedRevision)
{
  auto inventory = MakeInventory();
  inventory.layout_revision.front() ^= 0x80;
  const auto result = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kLayoutRevisionMismatch);
}

NOLINT_TEST(MaterialSlotInventoryTest, EmptyLayoutHasItsOwnCanonicalRevision)
{
  MaterialSlotInventory inventory { .geometry_asset_key = GeometryKey() };
  const auto canonical = WriteCanonicalMaterialSlotLayout(inventory.slots);
  ASSERT_TRUE(canonical.has_value());
  EXPECT_EQ(canonical.value(), "{\"schema_version\":1,\"slots\":[]}");
  const auto revision = ComputeMaterialSlotLayoutRevision(inventory.slots);
  ASSERT_TRUE(revision.has_value());
  EXPECT_EQ(DigestText(revision.value()),
    "28a67d261450344ebca126463605e51e98c97207b36ce27bd942110a09978b7a");
  inventory.layout_revision = revision.value();
  EXPECT_TRUE(
    ValidateMaterialSlotInventory(inventory, GeometryKey(), {}).has_value());
  const auto missing = ValidateMaterialSlotInventory(
    inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().code, ErrorCode::kMissingBinding);
}

NOLINT_TEST(
  MaterialSlotInventoryTest, CanonicalBindingOrderIsNumericAndLossless)
{
  auto inventory = MakeInventory();
  inventory.slots.resize(1);
  inventory.slots.front().bindings = { { 1, 10, {} }, { 1, 2, {} },
    { std::numeric_limits<uint32_t>::max(),
      std::numeric_limits<uint32_t>::max(), {} } };
  const auto canonical = WriteCanonicalMaterialSlotLayout(inventory.slots);
  ASSERT_TRUE(canonical.has_value());
  const auto index_two = canonical.value().find("\"submesh_index\":2,");
  const auto index_ten = canonical.value().find("\"submesh_index\":10,");
  ASSERT_NE(index_two, std::string::npos);
  ASSERT_NE(index_ten, std::string::npos);
  EXPECT_LT(index_two, index_ten);
  EXPECT_NE(canonical.value().find(
              "\"lod_index\":4294967295,\"submesh_index\":4294967295,"),
    std::string::npos);
}

} // namespace
