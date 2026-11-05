//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Types/MaterialShadingConstants.h>
#include <Oxygen/Vortex/test/Lighting/LightingGpuAbiFixture.h>

namespace oxygen::vortex::testing {
namespace {

  //! Mirrors `MaterialSurfaceProbeInput` in MaterialSurfaceProbe.hlsl.
  struct SurfaceInput {
    MaterialShadingConstants material {};
    glm::vec4 uvs { 0.0F };
    glm::vec2 uv_gradient { 0.0F };
    std::array<std::uint32_t, 2> reserved {};
  };
  static_assert(sizeof(SurfaceInput) == 304U);
  static_assert(offsetof(SurfaceInput, uvs) == 272U);
  static_assert(offsetof(SurfaceInput, uv_gradient) == 288U);

  struct SurfaceOutput {
    glm::vec4 base {};
    float metalness { 0.0F };
    float roughness { 0.0F };
    float ao { 0.0F };
    glm::vec3 emissive {};
    glm::vec3 normal {};
  };

  constexpr auto kSurfaceWords = 16U;
  constexpr auto kTolerance = 1.0e-3F;
  //! Hardware LOD and filter weights are approximate, within this.
  constexpr auto kFilterTolerance = 0.02F;

  constexpr auto kNearest = TextureSamplerState {
    .mag_filter = TextureFilter::kNearest,
    .min_filter = TextureFilter::kNearest,
    .mip_filter = TextureMipFilter::kNone,
  };

  [[nodiscard]] constexpr auto Pack(const std::uint8_t r, const std::uint8_t g,
    const std::uint8_t b, const std::uint8_t a = 255U) -> std::uint32_t
  {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8U)
      | (static_cast<std::uint32_t>(b) << 16U)
      | (static_cast<std::uint32_t>(a) << 24U);
  }

  [[nodiscard]] constexpr auto Grey(const std::uint8_t value) -> std::uint32_t
  {
    return Pack(value, value, value);
  }

  [[nodiscard]] auto Unorm(const std::uint8_t code) -> float
  {
    return static_cast<float>(code) / 255.0F;
  }

  //! A tangent-space direction as a normal map stores it.
  [[nodiscard]] auto EncodeNormal(const glm::vec3 normal) -> std::uint32_t
  {
    const auto code = [](const float value) -> std::uint8_t {
      return static_cast<std::uint8_t>(
        std::lround(((value * 0.5F) + 0.5F) * 255.0F));
    };
    return Pack(code(normal.x), code(normal.y), code(normal.z));
  }

  [[nodiscard]] auto SampleAt(const float u, const float v = 0.0F,
    const TextureSamplerState& sampler = kNearest) -> MaterialSlotSampling
  {
    return MakeMaterialSlotSampling(
      0U, { 1.0F, 1.0F }, { u, v }, 0.0F, sampler);
  }

  class MaterialSlotSamplingTest : public LightingGpuAbiTest {
  protected:
    auto Evaluate(const std::span<const SurfaceInput> inputs)
      -> std::vector<SurfaceOutput>
    {
      const auto words = Decode({
        .records = std::as_bytes(inputs),
        .stride = sizeof(SurfaceInput),
        .decoded_words = kSurfaceWords,
        .count = static_cast<std::uint32_t>(inputs.size()),
        .material_surface_probe = true,
      });
      const auto word
        = [&](const std::size_t record, const std::size_t index) -> float {
        return std::bit_cast<float>(words.at((record * kSurfaceWords) + index));
      };
      auto outputs = std::vector<SurfaceOutput>(inputs.size());
      for (std::size_t record = 0U; record < inputs.size(); ++record) {
        outputs.at(record) = {
          .base = { word(record, 0U), word(record, 1U), word(record, 2U),
            word(record, 3U), },
          .metalness = word(record, 4U),
          .roughness = word(record, 5U),
          .ao = word(record, 6U),
          .emissive = { word(record, 8U), word(record, 9U), word(record, 10U) },
          .normal = { word(record, 12U), word(record, 13U), word(record, 14U) },
        };
      }
      return outputs;
    }

    //! A material that samples only base color, which the probe returns as is.
    [[nodiscard]] static auto BaseColorOnly(const ShaderVisibleIndex texture,
      const MaterialSlotSampling& sampling,
      const glm::vec4 uvs = glm::vec4 { 0 },
      const glm::vec2 uv_gradient = glm::vec2 { 0 }) -> SurfaceInput
    {
      auto input = SurfaceInput { .uvs = uvs, .uv_gradient = uv_gradient };
      input.material.base_color_texture_index = texture;
      input.material.base_color_sampling = sampling;
      return input;
    }
  };

  //! One ramp per slot, each slot on its own set, transform and sampler, and
  //! each landing on a different texel. A slot reading any other slot's record
  //! lands on a different value.
  NOLINT_TEST_F(MaterialSlotSamplingTest, EachSlotReadsThroughItsOwnRecord)
  {
    constexpr auto kBase = std::array<std::uint8_t, 4> { 11U, 22U, 33U, 44U };
    constexpr auto kMetal = std::array<std::uint8_t, 4> { 55U, 66U, 77U, 88U };
    constexpr auto kRough
      = std::array<std::uint8_t, 4> { 99U, 111U, 122U, 133U };
    constexpr auto kOcclusion
      = std::array<std::uint8_t, 4> { 144U, 155U, 166U, 177U };
    constexpr auto kEmissive
      = std::array<std::uint8_t, 4> { 188U, 199U, 211U, 222U };
    const auto publish_ramp
      = [&](const std::array<std::uint8_t, 4>& ramp) -> ShaderVisibleIndex {
      const auto texels = std::array {
        Grey(ramp[0]),
        Grey(ramp[1]),
        Grey(ramp[2]),
        Grey(ramp[3]),
      };
      return PublishPackedTexture(Format::kRGBA8UNorm, texels);
    };
    const auto flat = EncodeNormal({ 0.0F, 0.0F, 1.0F });
    const auto bent = EncodeNormal({ 0.6F, 0.0F, 0.8F });
    const auto normal_texels = std::array { flat, flat, flat, bent };

    auto input = SurfaceInput {};
    // Set 0 sits at u = 0 and set 1 at u = 0.25.
    input.uvs = { 0.0F, 0.0F, 0.25F, 0.0F };
    auto& material = input.material;
    material.base_color_texture_index = publish_ramp(kBase);
    // Set 0, offset: texel 0.
    material.base_color_sampling = MakeMaterialSlotSampling(
      0U, { 1.0F, 1.0F }, { 0.125F, 0.0F }, 0.0F, kNearest);
    material.metallic_texture_index = publish_ramp(kMetal);
    // Set 1, offset: texel 1.
    material.metallic_sampling = MakeMaterialSlotSampling(
      1U, { 1.0F, 1.0F }, { 0.125F, 0.0F }, 0.0F, kNearest);
    material.roughness_texture_index = publish_ramp(kRough);
    // Set 1, scaled: texel 2.
    material.roughness_sampling = MakeMaterialSlotSampling(
      1U, { 2.0F, 1.0F }, { 0.125F, 0.0F }, 0.0F, kNearest);
    material.ambient_occlusion_texture_index = publish_ramp(kOcclusion);
    // Set 1, rotated a quarter turn, so u no longer depends on it: texel 3.
    material.ambient_occlusion_sampling
      = MakeMaterialSlotSampling(1U, { 1.0F, 1.0F }, { 0.875F, 0.0F },
        std::numbers::pi_v<float> / 2.0F, kNearest);
    material.emissive_texture_index = publish_ramp(kEmissive);
    // Clamped past the end: texel 3, where repeating would give texel 0.
    auto clamped = kNearest;
    clamped.address_u = TextureAddress::kClamp;
    material.emissive_sampling = MakeMaterialSlotSampling(
      0U, { 1.0F, 1.0F }, { 5.0F, 0.0F }, 0.0F, clamped);
    material.normal_texture_index
      = PublishPackedTexture(Format::kRGBA8UNorm, normal_texels);
    // Mirrored past the end, u = 1.125 back to 0.875: texel 3.
    auto mirrored = kNearest;
    mirrored.address_u = TextureAddress::kMirror;
    material.normal_sampling = MakeMaterialSlotSampling(
      1U, { 1.0F, 1.0F }, { 0.875F, 0.0F }, 0.0F, mirrored);
    material.metalness = 1.0F;
    material.roughness = 1.0F;
    material.ambient_occlusion = 1.0F;
    material.emissive_factor = { 1.0F, 1.0F, 1.0F };

    const auto output = Evaluate(std::span(&input, 1U)).front();

    EXPECT_NEAR(output.base.r, Unorm(kBase[0]), kTolerance);
    EXPECT_NEAR(output.metalness, Unorm(kMetal[1]), kTolerance);
    EXPECT_NEAR(output.roughness, Unorm(kRough[2]), kTolerance);
    EXPECT_NEAR(output.ao, Unorm(kOcclusion[3]), kTolerance);
    EXPECT_NEAR(output.emissive.r, Unorm(kEmissive[3]), kTolerance);
    EXPECT_NEAR(output.normal.x, 0.6F, 0.01F);
    EXPECT_NEAR(output.normal.y, 0.0F, 0.01F);
    EXPECT_NEAR(output.normal.z, 0.8F, 0.01F);
  }

  //! A packed texture gives roughness and metalness one sample, through the
  //! roughness slot, and occlusion its own through its own record.
  NOLINT_TEST_F(MaterialSlotSamplingTest, PackedOcclusionIsItsOwnSample)
  {
    const auto texels = std::array {
      Pack(10U, 20U, 30U),
      Pack(40U, 50U, 60U),
      Pack(70U, 80U, 90U),
    };
    const auto packed = PublishPackedTexture(Format::kRGBA8UNorm, texels);
    auto input = SurfaceInput {};
    auto& material = input.material;
    material.flags = data::pak::render::kMaterialFlag_GltfOrmPacked;
    material.metalness = 1.0F;
    material.roughness = 1.0F;
    material.ambient_occlusion = 1.0F;
    material.roughness_texture_index = packed;
    material.metallic_texture_index = packed;
    material.ambient_occlusion_texture_index = packed;
    material.roughness_sampling = SampleAt(0.5F);
    material.metallic_sampling = SampleAt(0.1F);
    material.ambient_occlusion_sampling = SampleAt(0.9F);

    const auto output = Evaluate(std::span(&input, 1U)).front();

    EXPECT_NEAR(output.roughness, Unorm(50U), kTolerance);
    EXPECT_NEAR(output.metalness, Unorm(60U), kTolerance);
    EXPECT_NEAR(output.ao, Unorm(70U), kTolerance);
  }

  NOLINT_TEST_F(MaterialSlotSamplingTest, AddressModesRepeatMirrorAndClamp)
  {
    struct Case {
      float coordinate;
      TextureAddress address;
      bool second_texel;
    };
    constexpr auto kCases = std::array {
      Case {
        .coordinate = 1.25F,
        .address = TextureAddress::kRepeat,
        .second_texel = false,
      },
      Case {
        .coordinate = 1.25F,
        .address = TextureAddress::kMirror,
        .second_texel = true,
      },
      Case {
        .coordinate = 1.25F,
        .address = TextureAddress::kClamp,
        .second_texel = true,
      },
      Case {
        .coordinate = 2.25F,
        .address = TextureAddress::kRepeat,
        .second_texel = false,
      },
      Case {
        .coordinate = 2.25F,
        .address = TextureAddress::kMirror,
        .second_texel = false,
      },
      Case {
        .coordinate = 2.25F,
        .address = TextureAddress::kClamp,
        .second_texel = true,
      },
      Case {
        .coordinate = -0.25F,
        .address = TextureAddress::kRepeat,
        .second_texel = true,
      },
      Case {
        .coordinate = -0.25F,
        .address = TextureAddress::kMirror,
        .second_texel = false,
      },
      Case {
        .coordinate = -0.25F,
        .address = TextureAddress::kClamp,
        .second_texel = false,
      },
    };
    const auto first = Grey(50U);
    const auto second = Grey(200U);
    const auto row = std::array { first, second };
    const auto column = std::array { first, second };
    const auto wide = PublishPackedTexture(Format::kRGBA8UNorm, row);
    const auto tall = PublishPackedTexture(Format::kRGBA8UNorm, column, 2U);

    auto inputs = std::vector<SurfaceInput> {};
    for (const auto& test_case : kCases) {
      auto along_u = kNearest;
      along_u.address_u = test_case.address;
      inputs.push_back(
        BaseColorOnly(wide, SampleAt(test_case.coordinate, 0.0F, along_u)));
      // Each axis has its own mode: the other one stays at repeat.
      auto along_v = kNearest;
      along_v.address_v = test_case.address;
      inputs.push_back(
        BaseColorOnly(tall, SampleAt(0.0F, test_case.coordinate, along_v)));
    }

    const auto outputs = Evaluate(inputs);

    for (std::size_t index = 0U; index < kCases.size(); ++index) {
      SCOPED_TRACE(index);
      const auto expected = Unorm(kCases.at(index).second_texel ? 200U : 50U);
      EXPECT_NEAR(outputs.at(index * 2U).base.r, expected, kTolerance);
      EXPECT_NEAR(outputs.at((index * 2U) + 1U).base.r, expected, kTolerance);
    }
  }

  //! At u = 0.4 of two texels, linear reads 30% of the second texel and nearest
  //! none. The mag filter decides under no gradient, the min filter under a
  //! large one, including when the two differ and the texture has no mips.
  NOLINT_TEST_F(MaterialSlotSamplingTest, MagAndMinFiltersApplySeparately)
  {
    const auto row = std::array { Grey(0U), Grey(255U) };
    const auto texture = PublishPackedTexture(Format::kRGBA8UNorm, row);
    const auto sharp = 0.0F;
    const auto blended = 0.3F;
    constexpr auto kMinified = glm::vec2 { 2.0F, 2.0F };
    constexpr auto kMagnified = glm::vec2 { 0.0F, 0.0F };

    struct Case {
      TextureFilter mag;
      TextureFilter min;
      TextureMipFilter mip;
      glm::vec2 gradient;
      float expected;
    };
    const auto cases = std::array {
      Case {
        .mag = TextureFilter::kNearest,
        .min = TextureFilter::kLinear,
        .mip = TextureMipFilter::kNone,
        .gradient = kMagnified,
        .expected = sharp,
      },
      Case {
        .mag = TextureFilter::kLinear,
        .min = TextureFilter::kNearest,
        .mip = TextureMipFilter::kNone,
        .gradient = kMagnified,
        .expected = blended,
      },
      Case {
        .mag = TextureFilter::kNearest,
        .min = TextureFilter::kLinear,
        .mip = TextureMipFilter::kNone,
        .gradient = kMinified,
        .expected = blended,
      },
      Case {
        .mag = TextureFilter::kLinear,
        .min = TextureFilter::kNearest,
        .mip = TextureMipFilter::kNone,
        .gradient = kMinified,
        .expected = sharp,
      },
      Case {
        .mag = TextureFilter::kNearest,
        .min = TextureFilter::kLinear,
        .mip = TextureMipFilter::kLinear,
        .gradient = kMinified,
        .expected = blended,
      },
      Case {
        .mag = TextureFilter::kLinear,
        .min = TextureFilter::kNearest,
        .mip = TextureMipFilter::kNearest,
        .gradient = kMinified,
        .expected = sharp,
      },
    };
    auto inputs = std::vector<SurfaceInput> {};
    for (const auto& test_case : cases) {
      const auto sampler = TextureSamplerState {
        .mag_filter = test_case.mag,
        .min_filter = test_case.min,
        .mip_filter = test_case.mip,
      };
      inputs.push_back(BaseColorOnly(texture, SampleAt(0.4F, 0.0F, sampler),
        glm::vec4 { 0.0F }, test_case.gradient));
    }

    const auto outputs = Evaluate(inputs);

    for (std::size_t index = 0U; index < cases.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_NEAR(
        outputs.at(index).base.r, cases.at(index).expected, kFilterTolerance);
    }
  }

  //! At level 1.25 of the mip chain, no mip filter stays on the first level,
  //! nearest takes level 1, and linear blends levels 1 and 2.
  NOLINT_TEST_F(MaterialSlotSamplingTest, MipFilterSelectsLevels)
  {
    constexpr auto kFirst = std::uint8_t { 40U };
    constexpr auto kSecond = std::uint8_t { 120U };
    constexpr auto kThird = std::uint8_t { 240U };
    const auto colors
      = std::array { Grey(kFirst), Grey(kSecond), Grey(kThird) };
    const auto texture = PublishColorMipChain(colors);
    // The first level is 4 texels wide, so this gradient is 2^1.25 texels.
    const auto gradient = std::pow(2.0F, 1.25F) / 4.0F;

    auto inputs = std::vector<SurfaceInput> {};
    for (const auto mip : {
           TextureMipFilter::kNone,
           TextureMipFilter::kNearest,
           TextureMipFilter::kLinear,
         }) {
      const auto sampler = TextureSamplerState { .mip_filter = mip };
      inputs.push_back(BaseColorOnly(texture, SampleAt(0.0F, 0.0F, sampler),
        glm::vec4 { 0.0F }, glm::vec2 { gradient, gradient }));
    }

    const auto outputs = Evaluate(inputs);

    EXPECT_NEAR(outputs.at(0U).base.r, Unorm(kFirst), kFilterTolerance);
    EXPECT_NEAR(outputs.at(1U).base.r, Unorm(kSecond), kFilterTolerance);
    EXPECT_NEAR(outputs.at(2U).base.r,
      (0.75F * Unorm(kSecond)) + (0.25F * Unorm(kThird)), kFilterTolerance);
  }

  //! A rotated or mirrored normal map lights as if its image were turned that
  //! way. Each case renders next to a map, not transformed, that already stores
  //! the direction the surface should end up with. Tiling changes no direction.
  NOLINT_TEST_F(MaterialSlotSamplingTest, TransformedNormalMapsMatchReference)
  {
    constexpr auto kPi = std::numbers::pi_v<float>;
    constexpr auto kAuthored = glm::vec3 { 0.6F, 0.0F, 0.8F };
    struct Case {
      glm::vec2 scale;
      float rotation;
      //! kAuthored's xy read in the mesh's frame. uv' = R(a) * uv, so a
      //! direction along u' runs at -a in the mesh, and a mirrored u' runs
      //! back.
      glm::vec2 expected;
    };
    const auto sine = std::sin(kPi / 4.0F);
    const auto cases = std::array {
      Case {
        .scale = { 1.0F, 1.0F },
        .rotation = 0.0F,
        .expected = { 0.6F, 0.0F },
      },
      Case {
        .scale = { 3.0F, 3.0F },
        .rotation = 0.0F,
        .expected = { 0.6F, 0.0F },
      },
      Case {
        .scale = { 4.0F, 0.5F },
        .rotation = 0.0F,
        .expected = { 0.6F, 0.0F },
      },
      Case {
        .scale = { 1.0F, 1.0F },
        .rotation = kPi / 2.0F,
        .expected = { 0.0F, -0.6F },
      },
      Case {
        .scale = { 1.0F, 1.0F },
        .rotation = kPi,
        .expected = { -0.6F, 0.0F },
      },
      Case {
        .scale = { 1.0F, 1.0F },
        .rotation = kPi / 4.0F,
        .expected = { 0.6F * sine, -0.6F * sine },
      },
      Case {
        .scale = { -1.0F, 1.0F },
        .rotation = 0.0F,
        .expected = { -0.6F, 0.0F },
      },
      Case {
        .scale = { 2.0F, -1.0F },
        .rotation = 0.0F,
        .expected = { 0.6F, 0.0F },
      },
    };

    const auto authored_texel = std::array { EncodeNormal(kAuthored) };
    const auto authored
      = PublishPackedTexture(Format::kRGBA8UNorm, authored_texel);
    auto inputs = std::vector<SurfaceInput> {};
    const auto add_normal_map
      = [&](const ShaderVisibleIndex texture,
          const MaterialSlotSampling& sampling) -> void {
      auto input = SurfaceInput {};
      input.material.normal_scale = 1.0F;
      input.material.normal_texture_index = texture;
      input.material.normal_sampling = sampling;
      inputs.push_back(input);
    };
    for (const auto& test_case : cases) {
      add_normal_map(authored,
        MakeMaterialSlotSampling(
          0U, test_case.scale, { 0.0F, 0.0F }, test_case.rotation, kNearest));
      const auto reference_texel = std::array {
        EncodeNormal(
          { test_case.expected.x, test_case.expected.y, kAuthored.z }),
      };
      add_normal_map(PublishPackedTexture(Format::kRGBA8UNorm, reference_texel),
        SampleAt(0.0F));
    }

    const auto outputs = Evaluate(inputs);

    for (std::size_t index = 0U; index < cases.size(); ++index) {
      SCOPED_TRACE(index);
      const auto& transformed = outputs.at(index * 2U).normal;
      const auto& reference = outputs.at((index * 2U) + 1U).normal;
      EXPECT_NEAR(transformed.x, reference.x, 0.01F);
      EXPECT_NEAR(transformed.y, reference.y, 0.01F);
      EXPECT_NEAR(transformed.z, reference.z, 0.01F);
    }
  }

} // namespace
} // namespace oxygen::vortex::testing
