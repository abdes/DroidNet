//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace core = oxygen::data::pak::core;
namespace scripting = oxygen::data::pak::scripting;
namespace geometry = oxygen::data::pak::geometry;
namespace physics = oxygen::data::pak::physics;

using oxygen::data::pak::testing::Digest;
using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::WireBytes;

//! The pak header is its magic, format and content versions, identity and
//! zeroed reserve.
NOLINT_TEST(PakFormatLayoutTest, HeaderWireBytes)
{
  auto header = core::PakHeader {};
  header.version = 0x1234U;
  header.content_version = 0x5678U;
  for (size_t i = 0; i < header.source_identity.size(); ++i) {
    header.source_identity.at(i) = static_cast<uint8_t>(0x10U + i);
  }

  const auto expected = WireBytes {}
                          .Text(std::string_view { "OXPAK\0\0\0", 8 })
                          .U16(0x1234U)
                          .U16(0x5678U)
                          .Run(0x10U, 16)
                          .Zeros(228);

  EXPECT_EQ(Encoded(header), expected.Bytes());
}

//! The pak footer is its directory, regions, tables, browse index, catalog,
//! zeroed reserve, CRC32 and magic, in that order.
NOLINT_TEST(PakFormatLayoutTest, FooterWireBytes)
{
  auto footer = core::PakFooter {};
  auto value = uint64_t { 0x0101010101010100U };
  const auto next = [&value] -> uint64_t { return ++value; };
  footer.directory_offset = next();
  footer.directory_size = next();
  footer.asset_count = next();
  for (auto* region : {
         &footer.texture_region,
         &footer.buffer_region,
         &footer.audio_region,
         &footer.script_region,
         &footer.physics_region,
       }) {
    region->offset = next();
    region->size = next();
  }
  for (auto* table : {
         &footer.texture_table,
         &footer.buffer_table,
         &footer.audio_table,
         &footer.script_resource_table,
         &footer.physics_resource_table,
       }) {
    table->offset = next();
    table->count = static_cast<uint32_t>(next());
    table->entry_size = static_cast<uint32_t>(next());
  }
  footer.browse_index_offset = next();
  footer.browse_index_size = next();
  footer.catalog_offset = next();
  footer.catalog_size = next();
  footer.pak_crc32 = 0xA1B2C3D4U;

  value = 0x0101010101010100U;
  auto expected = WireBytes {};
  expected.U64(next()).U64(next()).U64(next());
  for (int region = 0; region < 5; ++region) {
    expected.U64(next()).U64(next());
  }
  for (int table = 0; table < 5; ++table) {
    expected.U64(next()).U32(next()).U32(next());
  }
  expected.U64(next()).U64(next()).U64(next()).U64(next());
  expected.Zeros(28).U32(0xA1B2C3D4U).Text("OXPAKEND");

  EXPECT_EQ(Encoded(footer), expected.Bytes());
}

//! A texture row is its payload location, shape, format, alignment and
//! digest.
NOLINT_TEST(PakFormatLayoutTest, TextureRowWireBytes)
{
  const auto row = core::TextureResourceDesc {
    .data_offset = 0x0102030405060708U,
    .size_bytes = 0x11121314U,
    .texture_type = 0x21U,
    .compression_type = core::kTextureCompressionBc7,
    .width = 0x31323334U,
    .height = 0x41424344U,
    .depth = 0x5152U,
    .array_layers = 0x6162U,
    .mip_levels = 0x7172U,
    .format = 0x81U,
    .alignment = 0x9192U,
    .content_hash = Digest(0xA0U),
  };

  const auto expected = WireBytes {}
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U8(0x21U)
                          .U8(core::kTextureCompressionBc7)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U16(0x5152U)
                          .U16(0x6162U)
                          .U16(0x7172U)
                          .U8(0x81U)
                          .U16(0x9192U)
                          .Run(0xA0U, 32);

  EXPECT_EQ(Encoded(row), expected.Bytes());
}

//! A buffer row is its payload location, usage, element shape and digest.
NOLINT_TEST(PakFormatLayoutTest, BufferRowWireBytes)
{
  const auto row = core::BufferResourceDesc {
    .data_offset = 0x0102030405060708U,
    .size_bytes = 0x11121314U,
    .usage_flags = 0x21222324U,
    .element_stride = 0x31323334U,
    .element_format = 0x41U,
    .content_hash = Digest(0x50U),
  };

  const auto expected = WireBytes {}
                          .U64(0x0102030405060708U)
                          .U32(0x11121314U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U8(0x41U)
                          .Run(0x50U, 32);

  EXPECT_EQ(Encoded(row), expected.Bytes());
}

//! A script parameter's bool is 0 or 1; any other byte is no value.
NOLINT_TEST(PakFormatLayoutTest, ScriptBoolParameterIsZeroOrOne)
{
  auto record = scripting::ScriptParamRecord {
    .key_offset = 1U,
    .type = scripting::ScriptParamType::kBool,
    .value = {},
  };

  record.value.as_bool = 1U;
  const auto one_is_valid = scripting::HasValidScriptParamValues(record);
  record.value.as_bool = 2U;
  const auto two_is_valid = scripting::HasValidScriptParamValues(record);

  EXPECT_TRUE(one_is_valid);
  EXPECT_FALSE(two_is_valid);
}

//! A reserve is zero bytes: one nonzero byte breaks it, and a float reserve
//! holding -0.0 is not zero.
NOLINT_TEST(PakFormatReservesTest, ReserveIsZeroBytes)
{
  const auto zeros = std::array<std::byte, 4> {};
  auto one = zeros;
  one.back() = std::byte { 1U };
  const auto negative_zero = std::array { -0.0F };

  EXPECT_TRUE(core::IsZeroReserve(zeros));
  EXPECT_FALSE(core::IsZeroReserve(one));
  EXPECT_FALSE(core::IsZeroReserve(std::as_bytes(std::span(negative_zero))));
}

//! A mesh's reserve is that of its active arm: the standard arm's reserve
//! overlaps the skinned arm's fields, so only a standard mesh checks it.
NOLINT_TEST(PakFormatReservesTest, MeshChecksItsActiveArm)
{
  auto standard = geometry::MeshDesc {};
  standard.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kStandard);
  auto skinned = geometry::MeshDesc {};
  skinned.mesh_type = static_cast<uint8_t>(oxygen::data::MeshType::kSkinned);
  skinned.info.skinned.joint_count = 3U;
  const auto standard_is_clean = geometry::HasZeroReserves(standard);
  std::span { standard.info.standard._reserved }.front() = 1U;

  EXPECT_TRUE(standard_is_clean);
  EXPECT_FALSE(geometry::HasZeroReserves(standard));
  EXPECT_TRUE(geometry::HasZeroReserves(skinned));
}

//! A shape's reserve is that of the arm its type selects.
NOLINT_TEST(PakFormatReservesTest, ShapeChecksItsActiveArm)
{
  auto params = physics::ShapeParams {};
  std::span { params.box.half_extents }[1] = 2.0F;

  EXPECT_TRUE(physics::HasZeroReserves(params, physics::ShapeType::kBox));
  // The box's extents lie where a sphere keeps its reserve.
  EXPECT_FALSE(physics::HasZeroReserves(params, physics::ShapeType::kSphere));
}

//! A compound child's tail reserve is always checked.
NOLINT_TEST(PakFormatReservesTest, CompoundChildChecksItsTail)
{
  auto child = physics::CompoundShapeChildDesc {};
  const auto clean = physics::HasZeroReserves(child);
  std::span { child._reserved }.back() = 1U;

  EXPECT_TRUE(clean);
  EXPECT_FALSE(physics::HasZeroReserves(child));
}

//! A backend-scalar reserve is that of the arm the scene's backend selects:
//! bytes one backend reserves are another's scalars.
NOLINT_TEST(PakFormatReservesTest, BackendScalarsCheckTheBackendsArm)
{
  using physics::PhysicsBackend;
  auto rigid_body = physics::RigidBodyBackendScalars {};
  rigid_body.physx.max_contact_impulse = 1.0F;
  auto character = physics::CharacterBackendScalars {};
  character.jolt.max_num_hits = 1U;
  auto joint = physics::JointBackendScalars {};
  joint.physx.inv_mass_scale0 = 1.0F;
  auto wheel = physics::VehicleWheelBackendScalars {};
  wheel.jolt.wheel_castor = 1.0F;

  EXPECT_TRUE(physics::HasZeroReserves(rigid_body, PhysicsBackend::kPhysX));
  EXPECT_FALSE(physics::HasZeroReserves(rigid_body, PhysicsBackend::kJolt));
  EXPECT_TRUE(physics::HasZeroReserves(character, PhysicsBackend::kJolt));
  EXPECT_FALSE(physics::HasZeroReserves(character, PhysicsBackend::kPhysX));
  EXPECT_TRUE(physics::HasZeroReserves(joint, PhysicsBackend::kPhysX));
  EXPECT_FALSE(physics::HasZeroReserves(joint, PhysicsBackend::kJolt));
  EXPECT_TRUE(physics::HasZeroReserves(wheel, PhysicsBackend::kJolt));
  EXPECT_FALSE(physics::HasZeroReserves(wheel, PhysicsBackend::kPhysX));
}

} // namespace
