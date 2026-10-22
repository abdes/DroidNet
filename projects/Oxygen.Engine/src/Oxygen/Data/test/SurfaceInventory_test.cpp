//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include <fmt/base.h>

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::data;
using ErrorCode = SurfaceValidationErrorCode;

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

auto Binding(const std::uint32_t lod, const std::uint32_t submesh,
  const AssetKey default_material = {}) -> SurfaceBinding
{
  return {
    .lod_index = LodIndex { lod },
    .submesh_index = SubmeshIndex { submesh },
    .default_material_key = default_material,
  };
}

auto Location(const std::uint32_t lod, const std::uint32_t submesh)
  -> SurfaceBindingLocation
{
  return {
    .lod_index = LodIndex { lod },
    .submesh_index = SubmeshIndex { submesh },
  };
}

auto GeometryBindings() -> std::array<SurfaceBinding, 4>
{
  return {
    Binding(0, 0, MaterialA()),
    Binding(0, 1),
    Binding(0, 2, MaterialA()),
    Binding(1, 0, MaterialB()),
  };
}

auto MakeInventory() -> SurfaceInventory
{
  SurfaceInventory inventory {
    .geometry_asset_key = GeometryKey(),
    .surfaces = {
      {
        .surface_id = SurfaceId::FromString(
          "ffeeddcc-bbaa-9988-7766-554433221100").value(),
        .display_name = "Surface",
        .bindings = { Binding(1, 0, MaterialB()), Binding(0, 2, MaterialA()) },
      },
      {
        .surface_id = SurfaceId::FromString(
          "00112233-4455-6677-8899-aabbccddeeff").value(),
        .display_name = "Surface",
        .bindings = { Binding(0, 1), Binding(0, 0, MaterialA()) },
      },
    },
  };
  inventory.layout_revision
    = ComputeSurfaceLayoutRevision(inventory.surfaces).value();
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

NOLINT_TEST(SurfaceInventoryTest, MatchesIndependentCanonicalSha256Vector)
{
  const auto inventory = MakeInventory();
  const auto canonical = WriteCanonicalSurfaceLayout(inventory.surfaces);
  ASSERT_TRUE(canonical.has_value());
  constexpr std::string_view kExpected
    = "{\"schema_version\":1,\"surfaces\":["
      "{\"surface_id\":\"00112233-4455-6677-8899-aabbccddeeff\",\"bindings\":["
      "{\"lod_index\":0,\"submesh_index\":0,\"default_material_key\":"
      "\"10213243-5465-7687-98a9-bacbdcedfe0f\"},"
      "{\"lod_index\":0,\"submesh_index\":1,\"default_material_key\":"
      "\"00000000-0000-0000-0000-000000000000\"}]},"
      "{\"surface_id\":\"ffeeddcc-bbaa-9988-7766-554433221100\",\"bindings\":["
      "{\"lod_index\":0,\"submesh_index\":2,\"default_material_key\":"
      "\"10213243-5465-7687-98a9-bacbdcedfe0f\"},"
      "{\"lod_index\":1,\"submesh_index\":0,\"default_material_key\":"
      "\"01234567-89ab-cdef-0123-456789abcdef\"}]}]}";
  EXPECT_EQ(canonical.value(), kExpected);
  EXPECT_EQ(canonical.value().size(), 551U);
  // Independent Python hashlib.sha256 over the literal 551 UTF-8 bytes above.
  EXPECT_EQ(DigestText(inventory.layout_revision),
    "5a0b506eb7d1073a0efb4d8358f8efac64f606bad409a2cdc007cf170fc0fcf0");
  EXPECT_TRUE(
    ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(SurfaceInventoryTest,
  PermutationsAndLabelsDoNotChangeRevisionOrMutateDisplayOrder)
{
  const auto original = MakeInventory();
  for (const auto reverse_surfaces : { false, true }) {
    for (const auto reverse_first : { false, true }) {
      for (const auto reverse_second : { false, true }) {
        auto inventory = original;
        if (reverse_first) {
          std::ranges::reverse(inventory.surfaces.front().bindings);
        }
        if (reverse_second) {
          std::ranges::reverse(inventory.surfaces.back().bindings);
        }
        if (reverse_surfaces) {
          std::ranges::reverse(inventory.surfaces);
        }
        inventory.surfaces.front().display_name = "Renamed \"label\"\n";
        inventory.surfaces.back().display_name.clear();
        const auto first_id = inventory.surfaces.front().surface_id;
        const auto first_bindings = inventory.surfaces.front().bindings;
        const auto revision = ComputeSurfaceLayoutRevision(inventory.surfaces);
        ASSERT_TRUE(revision.has_value());
        EXPECT_EQ(revision.value(), original.layout_revision);
        EXPECT_EQ(inventory.surfaces.front().surface_id, first_id);
        EXPECT_EQ(inventory.surfaces.front().bindings, first_bindings);
        EXPECT_TRUE(
          ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings())
            .has_value());
      }
    }
  }
}

NOLINT_TEST(SurfaceInventoryTest, EveryLayoutIdentityFieldAffectsRevision)
{
  struct Mutation {
    std::string_view name;
    void (*apply)(SurfaceInventory&);
  };
  const std::array cases {
    Mutation {
      "default key",
      [](auto& value) -> auto {
        value.surfaces.front().bindings.front().default_material_key
          = MaterialA();
      },
    },
    Mutation {
      "LOD",
      [](auto& value) -> auto {
        value.surfaces.front().bindings.front().lod_index
          = oxygen::data::LodIndex { 2U };
      },
    },
    Mutation {
      "submesh",
      [](auto& value) -> auto {
        value.surfaces.front().bindings.back().submesh_index
          = oxygen::data::SubmeshIndex { 3U };
      },
    },
    Mutation {
      "surface ID",
      [](auto& value) -> auto {
        value.surfaces.front().surface_id
          = SurfaceId::FromString("11223344-5566-7788-99aa-bbccddeeff00")
              .value();
      },
    },
    Mutation {
      "surface owner",
      [](auto& value) -> auto {
        value.surfaces.back().bindings.push_back(
          value.surfaces.front().bindings.back());
        value.surfaces.front().bindings.pop_back();
      },
    },
  };
  for (const auto& mutation : cases) {
    SCOPED_TRACE(mutation.name);
    auto inventory = MakeInventory();
    mutation.apply(inventory);
    const auto changed = ComputeSurfaceLayoutRevision(inventory.surfaces);
    ASSERT_TRUE(changed.has_value());
    EXPECT_NE(changed.value(), inventory.layout_revision);
  }
}

NOLINT_TEST(SurfaceInventoryTest,
  DuplicateLabelsAndDefaultsRemainSeparateAndPerBindingDefaultsMayDiffer)
{
  const auto inventory = MakeInventory();
  ASSERT_EQ(inventory.surfaces.size(), 2U);
  EXPECT_EQ(inventory.surfaces.front().display_name,
    inventory.surfaces.back().display_name);
  EXPECT_NE(inventory.surfaces.front().surface_id,
    inventory.surfaces.back().surface_id);
  EXPECT_NE(inventory.surfaces.front().bindings.front().default_material_key,
    inventory.surfaces.front().bindings.back().default_material_key);
  EXPECT_EQ(inventory.surfaces.front().bindings.back().default_material_key,
    inventory.surfaces.back().bindings.back().default_material_key);
  EXPECT_TRUE(
    inventory.surfaces.back().bindings.front().default_material_key.IsNil());
  EXPECT_TRUE(
    ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(SurfaceInventoryTest, RejectsInvalidSurfaceOwnershipBeforeHashing)
{
  struct InvalidCase {
    std::string_view name;
    void (*apply)(SurfaceInventory&);
    ErrorCode expected;
  };
  const std::array cases {
    InvalidCase {
      "nil ID",
      [](auto& value) -> auto { value.surfaces.front().surface_id = {}; },
      ErrorCode::kNilSurfaceId,
    },
    InvalidCase {
      "duplicate ID",
      [](auto& value) -> auto {
        value.surfaces.front().surface_id = value.surfaces.back().surface_id;
      },
      ErrorCode::kDuplicateSurfaceId,
    },
    InvalidCase {
      "unbound surface",
      [](auto& value) -> auto { value.surfaces.front().bindings.clear(); },
      ErrorCode::kEmptySurfaceBindings,
    },
    InvalidCase {
      "duplicate within surface",
      [](auto& value) -> auto {
        value.surfaces.front().bindings.push_back(
          value.surfaces.front().bindings.front());
      },
      ErrorCode::kDuplicateBinding,
    },
    InvalidCase {
      "duplicate across surfaces",
      [](auto& value) -> auto {
        value.surfaces.front().bindings.push_back(
          value.surfaces.back().bindings.front());
      },
      ErrorCode::kDuplicateBinding,
    },
  };
  for (const auto& invalid : cases) {
    SCOPED_TRACE(invalid.name);
    auto inventory = MakeInventory();
    invalid.apply(inventory);
    const auto canonical = WriteCanonicalSurfaceLayout(inventory.surfaces);
    ASSERT_FALSE(canonical.has_value());
    EXPECT_EQ(canonical.error().code, invalid.expected);
    const auto revision = ComputeSurfaceLayoutRevision(inventory.surfaces);
    ASSERT_FALSE(revision.has_value());
    EXPECT_EQ(revision.error().code, invalid.expected);
    const auto validation
      = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(validation.has_value());
    EXPECT_EQ(validation.error().code, invalid.expected);
  }
}

NOLINT_TEST(SurfaceInventoryTest, RequiresExactGeometryIdentityAndSchema)
{
  auto inventory = MakeInventory();
  for (const auto version : { 0U, 2U, std::numeric_limits<uint32_t>::max() }) {
    inventory.schema_version = version;
    const auto result
      = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kUnsupportedSchemaVersion);
  }
  inventory.schema_version = kSurfaceInventorySchemaVersion;
  auto result = ValidateSurfaceInventory(inventory, {}, GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kNilGeometryKey);
  inventory.geometry_asset_key = {};
  result
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kNilGeometryKey);
  inventory.geometry_asset_key = MaterialA();
  result
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kGeometryIdentityMismatch);
  // Geometry scope is validated separately; it is not part of the layout hash.
  EXPECT_TRUE(
    ValidateSurfaceInventory(inventory, MaterialA(), GeometryBindings())
      .has_value());
}

NOLINT_TEST(SurfaceInventoryTest, RejectsMissingAndOutOfRangeSurfaces)
{
  auto inventory = MakeInventory();
  inventory.surfaces.front().bindings.pop_back();
  auto result
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kMissingBinding);
  ASSERT_TRUE(result.error().binding.has_value());
  EXPECT_EQ(result.error().binding.value(), Location(0, 2));
  for (const auto bad_lod : { false, true }) {
    inventory = MakeInventory();
    auto& binding = inventory.surfaces.front().bindings.front();
    if (bad_lod) {
      binding.lod_index = oxygen::data::LodIndex { 2U };
    } else {
      binding.submesh_index = oxygen::data::SubmeshIndex { 1U };
    }
    result
      = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::kUnknownBinding);
    EXPECT_EQ(result.error().surface_id, inventory.surfaces.front().surface_id);
    EXPECT_EQ(result.error().binding,
      (SurfaceBindingLocation { binding.lod_index, binding.submesh_index }));
  }
}

NOLINT_TEST(SurfaceInventoryTest, RejectsAmbiguousGeometryAndWrongDefaults)
{
  auto inventory = MakeInventory();
  auto bindings = GeometryBindings();
  bindings.back() = bindings.front();
  auto result = ValidateSurfaceInventory(inventory, GeometryKey(), bindings);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kDuplicateGeometryBinding);
  EXPECT_EQ(result.error().binding, Location(0, 0));

  inventory.surfaces.front().bindings.front().default_material_key = {};
  result
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kDefaultMaterialMismatch);
  EXPECT_EQ(result.error().binding, Location(1, 0));
}

NOLINT_TEST(SurfaceInventoryTest, RejectsStaleRecordedRevision)
{
  auto inventory = MakeInventory();
  inventory.layout_revision.front() ^= 0x80;
  const auto result
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kLayoutRevisionMismatch);
}

NOLINT_TEST(SurfaceInventoryTest, EmptyLayoutHasItsOwnCanonicalRevision)
{
  SurfaceInventory inventory { .geometry_asset_key = GeometryKey() };
  const auto canonical = WriteCanonicalSurfaceLayout(inventory.surfaces);
  ASSERT_TRUE(canonical.has_value());
  EXPECT_EQ(canonical.value(), "{\"schema_version\":1,\"surfaces\":[]}");
  const auto revision = ComputeSurfaceLayoutRevision(inventory.surfaces);
  ASSERT_TRUE(revision.has_value());
  EXPECT_EQ(DigestText(revision.value()),
    "71adc4653f3830d4bf0e1c13e806c0a00cae3c54ae7dd953f49e9f83c40d94fe");
  inventory.layout_revision = revision.value();
  EXPECT_TRUE(
    ValidateSurfaceInventory(inventory, GeometryKey(), {}).has_value());
  const auto missing
    = ValidateSurfaceInventory(inventory, GeometryKey(), GeometryBindings());
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(missing.error().code, ErrorCode::kMissingBinding);
}

NOLINT_TEST(SurfaceInventoryTest, CanonicalBindingOrderIsNumericAndLossless)
{
  auto inventory = MakeInventory();
  inventory.surfaces.resize(1);
  inventory.surfaces.front().bindings = {
    Binding(1, 10),
    Binding(1, 2),
    Binding((std::numeric_limits<std::uint32_t>::max)(),
      (std::numeric_limits<std::uint32_t>::max)()),
  };
  const auto canonical = WriteCanonicalSurfaceLayout(inventory.surfaces);
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
