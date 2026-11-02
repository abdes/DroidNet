//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>

#include "PakFormatWireBytes.h"

#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Testing/GTest.h>

namespace {

namespace render = oxygen::data::pak::render;

using oxygen::data::HalfFloat;
using oxygen::data::ResourceReferenceIndex;
using oxygen::data::Unorm16;

using oxygen::data::pak::testing::Encoded;
using oxygen::data::pak::testing::HeaderBytes;
using oxygen::data::pak::testing::MakeHeader;
using oxygen::data::pak::testing::RawBytes;
using oxygen::data::pak::testing::WireBytes;

//! A normalized 16-bit value with the given bit pattern.
auto Unorm(const uint16_t bits) -> Unorm16 { return Unorm16 { bits }; }

//! A half float with the given bit pattern.
auto Half(const uint16_t bits) -> HalfFloat { return HalfFloat { bits }; }

//! A shader reference is its stage, three string offsets and source hash.
NOLINT_TEST(PakFormatLayoutRenderTest, ShaderReferenceDescWireBytes)
{
  const auto reference = render::ShaderReferenceDesc {
    .shader_type = 0x11U,
    .source_path_offset = 0x21222324U,
    .entry_point_offset = 0x31323334U,
    .defines_offset = 0x41424344U,
    .shader_hash = 0x5152535455565758U,
  };

  const auto expected = WireBytes {}
                          .U8(0x11U)
                          .U32(0x21222324U)
                          .U32(0x31323334U)
                          .U32(0x41424344U)
                          .U64(0x5152535455565758U);

  EXPECT_EQ(Encoded(reference), expected.Bytes());
}

//! A material is its header, domain, flags, shader stages, base color and
//! factors, twelve texture references, the transmission and surface factors,
//! the UV transform and the procedural grid block, in that order.
NOLINT_TEST(PakFormatLayoutRenderTest, MaterialAssetDescWireBytes)
{
  const auto material = render::MaterialAssetDesc {
    .header = MakeHeader(),
    .material_domain = 0x81U,
    .flags = 0x82838485U,
    .shader_stages = 0x86878889U,
    .base_color = { 1.5F, 2.5F, 3.5F, 4.5F, },
    .normal_scale = 5.5F,
    .metalness = Unorm(0x1112U),
    .roughness = Unorm(0x2122U),
    .ambient_occlusion = Unorm(0x3132U),
    .base_color_texture = ResourceReferenceIndex { 0x41424301U },
    .normal_texture = ResourceReferenceIndex { 0x41424302U },
    .metallic_texture = ResourceReferenceIndex { 0x41424303U },
    .roughness_texture = ResourceReferenceIndex { 0x41424304U },
    .ambient_occlusion_texture = ResourceReferenceIndex { 0x41424305U },
    .emissive_texture = ResourceReferenceIndex { 0x51525301U },
    .specular_texture = ResourceReferenceIndex { 0x51525302U },
    .sheen_color_texture = ResourceReferenceIndex { 0x51525303U },
    .clearcoat_texture = ResourceReferenceIndex { 0x51525304U },
    .clearcoat_normal_texture = ResourceReferenceIndex { 0x51525305U },
    .transmission_texture = ResourceReferenceIndex { 0x51525306U },
    .thickness_texture = ResourceReferenceIndex { 0x51525307U },
    .emissive_factor = { 6.5F, 7.5F, 8.5F, },
    .alpha_cutoff = Unorm(0x4142U),
    .ior = 9.5F,
    .specular_factor = Unorm(0x5152U),
    .sheen_color_factor = { Half(0x6162U), Half(0x6364U), Half(0x6566U), },
    .clearcoat_factor = Unorm(0x7172U),
    .clearcoat_roughness = Unorm(0x7374U),
    .transmission_factor = Unorm(0x7576U),
    .thickness_factor = Unorm(0x7778U),
    .attenuation_color = { Half(0x8182U), Half(0x8384U), Half(0x8586U), },
    .attenuation_distance = 10.5F,
    .uv_scale = { 11.5F, 12.5F, },
    .uv_offset = { 13.5F, 14.5F, },
    .uv_rotation_radians = 15.5F,
    .uv_set = 0x91U,
    .grid_spacing = { 16.5F, 17.5F, },
    .grid_major_every = 0xA1A2A3A4U,
    .grid_line_thickness = 18.5F,
    .grid_major_thickness = 19.5F,
    .grid_axis_thickness = 20.5F,
    .grid_fade_start = 21.5F,
    .grid_fade_end = 22.5F,
    .grid_minor_color = { 23.5F, 24.5F, 25.5F, 26.5F, },
    .grid_major_color = { 27.5F, 28.5F, 29.5F, 30.5F, },
    .grid_axis_color_x = { 31.5F, 32.5F, 33.5F, 34.5F, },
    .grid_axis_color_y = { 35.5F, 36.5F, 37.5F, 38.5F, },
    .grid_origin_color = { 39.5F, 40.5F, 41.5F, 42.5F, },
  };

  auto expected = WireBytes {};
  expected.Append(HeaderBytes())
    .U8(0x81U)
    .U32(0x82838485U)
    .U32(0x86878889U)
    .Floats(1.5F, 4)
    .F32(5.5F)
    .U16(0x1112U)
    .U16(0x2122U)
    .U16(0x3132U)
    .U32(0x41424301U)
    .U32(0x41424302U)
    .U32(0x41424303U)
    .U32(0x41424304U)
    .U32(0x41424305U)
    .U32(0x51525301U)
    .U32(0x51525302U)
    .U32(0x51525303U)
    .U32(0x51525304U)
    .U32(0x51525305U)
    .U32(0x51525306U)
    .U32(0x51525307U)
    .Floats(6.5F, 3)
    .U16(0x4142U)
    .F32(9.5F)
    .U16(0x5152U)
    .U16(0x6162U)
    .U16(0x6364U)
    .U16(0x6566U)
    .U16(0x7172U)
    .U16(0x7374U)
    .U16(0x7576U)
    .U16(0x7778U)
    .U16(0x8182U)
    .U16(0x8384U)
    .U16(0x8586U)
    .F32(10.5F)
    .Floats(11.5F, 2)
    .Floats(13.5F, 2)
    .F32(15.5F)
    .U8(0x91U)
    .Floats(16.5F, 2)
    .U32(0xA1A2A3A4U)
    .Floats(18.5F, 5)
    .Floats(23.5F, 4)
    .Floats(27.5F, 4)
    .Floats(31.5F, 4)
    .Floats(35.5F, 4)
    .Floats(39.5F, 4);

  EXPECT_EQ(Encoded(material), expected.Bytes());
}

//! A subresource layout is its offset, row pitch and size.
NOLINT_TEST(PakFormatLayoutRenderTest, SubresourceLayoutWireBytes)
{
  const auto layout = render::SubresourceLayout {
    .offset_bytes = 0x11121314U,
    .row_pitch_bytes = 0x21222324U,
    .size_bytes = 0x31323334U,
  };

  const auto expected
    = WireBytes {}.U32(0x11121314U).U32(0x21222324U).U32(0x31323334U);

  EXPECT_EQ(Encoded(layout), expected.Bytes());
}

//! A texture payload header is its magic, packing policy, flags, subresource
//! count, total size and the offsets of the layouts and the data. It is not a
//! Serio record, so its in-memory packed bytes are compared.
NOLINT_TEST(PakFormatLayoutRenderTest, TexturePayloadHeaderWireBytes)
{
  const auto header = render::TexturePayloadHeader {
    .magic = 0x01020304U,
    .packing_policy = 0x11U,
    .flags = 0x21U,
    .subresource_count = 0x3132U,
    .total_payload_size = 0x41424344U,
    .layouts_offset_bytes = 0x51525354U,
    .data_offset_bytes = 0x61626364U,
  };

  const auto expected = WireBytes {}
                          .U32(0x01020304U)
                          .U8(0x11U)
                          .U8(0x21U)
                          .U16(0x3132U)
                          .U32(0x41424344U)
                          .U32(0x51525354U)
                          .U32(0x61626364U);

  EXPECT_EQ(RawBytes(header), expected.Bytes());
}

} // namespace
