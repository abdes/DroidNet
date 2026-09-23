//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialDecode.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/MaterialEvaluation.h>

namespace oxygen::vortex::testing {
namespace {
  using Rgba8 = std::array<std::uint8_t, 4>;

  // Independent mode-6 fixture writer: one subset, 7-bit RGBA endpoints,
  // one shared low bit per endpoint and 4-bit indices (3 at the anchor).
  // https://learn.microsoft.com/windows/win32/direct3d11/bc7-format-mode-reference
  auto EncodeBc7(const std::array<Rgba8, 2>& endpoints,
    const std::array<std::uint8_t, 16>& selectors)
    -> std::array<std::uint8_t, 16>
  {
    constexpr unsigned kByteBits = 8U;
    constexpr unsigned kEndpointBits = 7U;
    constexpr unsigned kMode = 6U;
    constexpr unsigned kSelectorBits = 4U;
    auto block = std::array<std::uint8_t, 16> {};
    unsigned cursor = 0U;
    const auto put = [&](const unsigned value, const unsigned bits) -> void {
      for (unsigned bit = 0U; bit < bits; ++bit, ++cursor) {
        block.at(cursor / kByteBits) |= static_cast<std::uint8_t>(
          ((value >> bit) & 1U) << (cursor % kByteBits));
      }
    };
    put(1U << kMode, kMode + 1U);
    for (std::size_t channel = 0U; channel < Rgba8 {}.size(); ++channel) {
      for (const auto& endpoint : endpoints) {
        CHECK_F((endpoint.at(channel) & 1U) == (endpoint.front() & 1U));
        put(endpoint.at(channel) >> 1U, kEndpointBits);
      }
    }
    for (const auto& endpoint : endpoints) {
      put(endpoint.front() & 1U, 1U);
    }
    CHECK_F(selectors.front() < (1U << (kSelectorBits - 1U)));
    for (std::size_t pixel = 0U; pixel < selectors.size(); ++pixel) {
      put(
        selectors.at(pixel), pixel == 0U ? kSelectorBits - 1U : kSelectorBits);
    }
    CHECK_F(cursor == block.size() * kByteBits);
    return block;
  }
  enum class MaterialSlot : std::uint8_t {
    kBaseColor,
    kNormal,
    kMetallic,
    kRoughness,
    kOcclusion,
    kEmissive,
    kCount,
  };
  enum class TextureLayout : std::uint8_t {
    kSeparate,
    kOrm,
    kOrmSeparateAo,
    kDisabled,
  };
  enum class SurfaceCase : std::uint8_t {
    kFront,
    kBackTwoSided,
    kBackCulled,
    kMaskedKept,
    kMaskedDiscarded,
    kTilted,
  };

  class MaterialRasterReferenceTest : public exposure::ExposureLightingGpuTest {
  protected:
    auto AddEncodedTile(const Format format,
      const std::array<Rgba8, 16>& pixels,
      const std::array<Rgba8, 2>& endpoints,
      const std::array<std::uint8_t, 16>& selectors) -> content::ResourceKey
    {
      const bool compressed
        = format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
      auto desc = data::pak::core::TextureResourceDesc {};
      desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
      desc.width = desc.height = 4U;
      desc.depth = desc.array_layers = desc.mip_levels = 1U;
      desc.format = static_cast<std::uint8_t>(format);
      constexpr auto kPayloadAlignment = 256U;
      constexpr auto kBc7Compression = 7U;
      desc.alignment = kPayloadAlignment;
      desc.compression_type = compressed ? kBc7Compression : 0U;
      const auto encoded = EncodeBc7(endpoints, selectors);
      auto bytes = std::vector<std::uint8_t>(
        compressed ? encoded.size() : sizeof(pixels));
      if (compressed) {
        std::ranges::copy(encoded, bytes.begin());
      } else {
        std::memcpy(bytes.data(), pixels.data(), bytes.size());
      }
      const auto layouts = std::array {
        data::pak::render::SubresourceLayout {
          .offset_bytes = 0U,
          .row_pitch_bytes = 16U,
          .size_bytes = static_cast<std::uint32_t>(bytes.size()),
        },
      };
      return AddTexture(desc, layouts, bytes);
    }
    auto AddTexture(const exposure::Pixel& pixel) -> content::ResourceKey
    {
      auto desc = data::pak::core::TextureResourceDesc {};
      desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
      desc.width = desc.height = desc.depth = desc.mip_levels
        = desc.array_layers = 1U;
      desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
      constexpr std::uint32_t alignment = 256U;
      desc.alignment = alignment;
      auto bytes = std::vector<std::uint8_t>(sizeof(pixel));
      std::memcpy(bytes.data(), pixel.data(), sizeof(pixel));
      const auto layouts = std::array {
        data::pak::render::SubresourceLayout {
          .offset_bytes = 0U,
          .row_pitch_bytes = sizeof(pixel),
          .size_bytes = sizeof(pixel),
        },
      };
      return AddTexture(desc, layouts, bytes);
    }

    auto AddTexture(data::pak::core::TextureResourceDesc desc,
      const std::span<const data::pak::render::SubresourceLayout> layouts,
      const std::span<const std::uint8_t> bytes) -> content::ResourceKey
    {
      const auto key = owned_asset_loader_->MintSyntheticTextureKey();
      desc.content_hash = key.get();
      auto payload = detail::BuildV4TexturePayload(desc, layouts, bytes);
      desc.size_bytes = static_cast<std::uint32_t>(payload.size());
      owned_asset_loader_->SetTexture(
        key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
      return key;
    }

    auto ReadPacked(const graphics::Texture& texture, std::uint32_t& word)
      -> void
    {
      ASSERT_EQ(texture.GetDescriptor().width, 1U);
      ASSERT_EQ(texture.GetDescriptor().height, 1U);
      auto readback = GetReadbackManager()->CreateTextureReadback(
        "Material reference texel");
      {
        auto recorder = AcquireRecorder("Material reference readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(texture));
        ASSERT_TRUE(readback->EnqueueCopy(*recorder, texture, {}).has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      const auto bytes = MappedTextureBytes(*mapped, sizeof(word));
      ASSERT_GE(bytes.size(), sizeof(word));
      std::memcpy(&word, bytes.data(), sizeof(word));
    }
  };

  NOLINT_TEST_F(
    MaterialRasterReferenceTest, SampledSurfaceMatchesIndependentMaterialOracle)
  {
    const auto base_sample = exposure::Pixel { 0.5F, 0.25F, 0.75F, 0.8F };
    const auto normal_sample = exposure::Pixel { 0.75F, 0.25F, 1.0F, 1.0F };
    const auto emission_sample = exposure::Pixel { 3.0F, 0.5F, 0.25F, 1.0F };
    const auto orm_sample = exposure::Pixel { 0.2F, 0.6F, 0.8F, 1.0F };
    const auto base_key = AddTexture(base_sample);
    const auto normal_key = AddTexture(normal_sample);
    const auto metallic_key = AddTexture({ 0.25F, 0.1F, 0.9F, 1.0F });
    const auto roughness_key = AddTexture({ 0.75F, 0.2F, 0.1F, 1.0F });
    const auto occlusion_key = AddTexture({ 0.4F, 0.3F, 0.9F, 1.0F });
    const auto emission_key = AddTexture(emission_sample);
    const auto orm_key = AddTexture(orm_sample);
    std::array<std::shared_ptr<const graphics::Texture>, 3> surfaces;
    probe->inspect = [&](const auto&, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto& textures = owner->GetSceneTextures();
      surfaces = {
        textures.GetGBufferResource(GBufferIndex::kNormal),
        textures.GetGBufferResource(GBufferIndex::kMaterial),
        textures.GetGBufferResource(GBufferIndex::kBaseColor),
      };
    };
    double maximum_code_error = 0.0;
    unsigned checked = 0U;
    unsigned rejected = 0U;
    for (const auto layout : {
           TextureLayout::kSeparate,
           TextureLayout::kOrm,
           TextureLayout::kOrmSeparateAo,
           TextureLayout::kDisabled,
         }) {
      for (const auto surface_case : {
             SurfaceCase::kFront,
             SurfaceCase::kBackTwoSided,
             SurfaceCase::kBackCulled,
             SurfaceCase::kMaskedKept,
             SurfaceCase::kMaskedDiscarded,
             SurfaceCase::kTilted,
           }) {
        for (const auto normal_scale :
          { 0.0F, 0.5F, 1.0F, 2.0F, 4.0F, 10.0F }) {
          SCOPED_TRACE(static_cast<unsigned>(layout));
          SCOPED_TRACE(static_cast<unsigned>(surface_case));
          SCOPED_TRACE(normal_scale);
          const bool back = surface_case == SurfaceCase::kBackTwoSided
            || surface_case == SurfaceCase::kBackCulled;
          const bool masked = surface_case == SurfaceCase::kMaskedKept
            || surface_case == SurfaceCase::kMaskedDiscarded;
          const bool packed = layout == TextureLayout::kOrm
            || layout == TextureLayout::kOrmSeparateAo;
          auto desc = data::pak::render::MaterialAssetDesc {};
          desc.material_domain
            = static_cast<std::uint8_t>(masked ? data::MaterialDomain::kMasked
                                               : data::MaterialDomain::kOpaque);
          desc.flags = surface_case == SurfaceCase::kBackCulled
            ? 0U
            : data::pak::render::kMaterialFlag_DoubleSided;
          if (masked) {
            desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
          }
          if (packed) {
            desc.flags |= data::pak::render::kMaterialFlag_GltfOrmPacked;
          }
          if (layout == TextureLayout::kDisabled) {
            desc.flags |= data::pak::render::kMaterialFlag_NoTextureSampling;
          }
          desc.base_color[0] = 0.8F;
          desc.base_color[1] = 0.6F;
          desc.base_color[2] = 0.4F;
          desc.base_color[3]
            = surface_case == SurfaceCase::kMaskedDiscarded ? 0.25F : 0.75F;
          desc.metalness = data::Unorm16 { 0.6F };
          desc.roughness = data::Unorm16 { 0.8F };
          desc.ambient_occlusion = data::Unorm16 { 0.5F };
          desc.normal_scale = normal_scale;
          desc.emissive_factor[0] = data::HalfFloat { 2.0F };
          desc.emissive_factor[1] = data::HalfFloat { 4.0F };
          desc.emissive_factor[2] = data::HalfFloat { 8.0F };
          auto keys = std::vector<content::ResourceKey>(
            static_cast<std::size_t>(MaterialSlot::kCount));
          keys.at(static_cast<std::size_t>(MaterialSlot::kBaseColor))
            = base_key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kNormal)) = normal_key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kMetallic))
            = packed ? orm_key : metallic_key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kRoughness))
            = packed ? orm_key : roughness_key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kOcclusion))
            = layout == TextureLayout::kOrm ? orm_key : occlusion_key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kEmissive))
            = emission_key;
          mesh_node.GetRenderable().SetMaterialOverride(0U, 0U,
            std::make_shared<data::MaterialAsset>(
              data::AssetKey::FromVirtualPath("/Test/Lighting/Material-"
                + std::to_string(++material_sequence) + ".omat"),
              desc, std::vector<data::ShaderReference> {}, std::move(keys)));
          auto input = reference::MaterialEvaluationInput {
            .factors = { .base_color = { .rgb = { .red = desc.base_color[0],
                  .green = desc.base_color[1], .blue = desc.base_color[2], }, .alpha = desc.base_color[3], },
              .metallic = static_cast<double>(desc.metalness.get()) / 65535.0,
              .roughness = reference::PerceptualRoughness { static_cast<double>(desc.roughness.get()) / 65535.0 },
              .ambient_occlusion = static_cast<double>(desc.ambient_occlusion.get()) / 65535.0,
              .emissive = { .red = 2.0, .green = 4.0, .blue = 8.0 }, .normal_scale = normal_scale, },
            .samples = {
              .base_color = reference::LinearRgba { .rgb = { .red = base_sample.at(0),
                  .green = base_sample.at(1), .blue = base_sample.at(2), }, .alpha = base_sample.at(3), },
              .normal = reference::LinearRgb { .red = normal_sample.at(0), .green = normal_sample.at(1), .blue = normal_sample.at(2) },
              .metallic = 0.25, .roughness = 0.75,
              .emissive = reference::LinearRgb { .red = emission_sample.at(0), .green = emission_sample.at(1), .blue = emission_sample.at(2) },
            },
            .textures_enabled = layout != TextureLayout::kDisabled,
            .double_sided = surface_case != SurfaceCase::kBackCulled,
            .front_face = !back,
            .alpha_test = masked,
            .alpha_cutoff = static_cast<double>(desc.alpha_cutoff.get()) / 65535.0,
          };
          if (packed) {
            input.samples.orm = reference::OrmSample {
              .occlusion = orm_sample.at(0),
              .roughness = orm_sample.at(1),
              .metallic = orm_sample.at(2),
            };
          }
          if (layout != TextureLayout::kOrm) {
            input.samples.occlusion = static_cast<double>(0.4F);
          }
          mesh_node.GetTransform().SetLocalRotation(
            back ? glm::quat { 0, 0, 1, 0 } : glm::quat { 1, 0, 0, 0 });
          mesh_node.GetTransform().SetLocalPosition(
            back ? glm::vec3 { 0, 0, -2 } : glm::vec3 { 0 });
          if (back) {
            input.basis = {
              .normal = { .x = 0, .y = 0, .z = -1 },
              .tangent = { -1, 0, 0 },
              .bitangent = { 0, 1, 0 },
            };
          }
          if (surface_case == SurfaceCase::kTilted) {
            const auto diagonal = std::sqrt(0.5);
            mesh_node.GetTransform().SetLocalRotation(glm::angleAxis(
              std::numbers::pi_v<float> / 4.0F, glm::vec3 { 0, 1, 0 }));
            mesh_node.GetTransform().SetLocalPosition(
              { static_cast<float>(diagonal), 0.0F,
                static_cast<float>(diagonal - 1.0) });
            input.basis = {
              .normal = { .x = diagonal, .y = 0, .z = diagonal },
              .tangent = { diagonal, 0, -diagonal },
              .bitangent = { 0, 1, 0 },
            };
          }
          const auto expected = reference::EvaluateMaterial(input);
          ASSERT_TRUE(expected.has_value());
          ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
          const auto emission = ReadFloatTexture(*probe->color);
          ASSERT_EQ(emission.size(), 1U);
          const auto predicted_emission = std::array {
            expected->emissive.red,
            expected->emissive.green,
            expected->emissive.blue,
          };
          for (std::size_t channel = 0U; channel < 3U; ++channel) {
            EXPECT_NEAR(emission.front().at(channel),
              expected->fragment_visible ? predicted_emission.at(channel) : 0.0,
              1.0e-5);
          }
          if (!expected->fragment_visible) {
            ++rejected;
            continue;
          }
          const auto formats = std::array {
            Format::kR10G10B10A2UNorm,
            Format::kRGBA8UNorm,
            Format::kRGBA8UNormSRGB,
          };
          auto words = std::array<std::uint32_t, 3> {};
          for (std::size_t index = 0U; index < surfaces.size(); ++index) {
            ASSERT_NE(surfaces.at(index), nullptr);
            ASSERT_EQ(
              surfaces.at(index)->GetDescriptor().format, formats.at(index));
            ASSERT_NO_FATAL_FAILURE(
              ReadPacked(*surfaces.at(index), words.at(index)));
          }
          const auto check_code
            = [&](const unsigned actual, const double predicted) -> void {
            const auto error
              = std::abs(static_cast<double>(actual) - predicted);
            // D3D FLOAT->UNORM/SRGB permits 0.6 stored-code error. The extra
            // 0.001 covers float material arithmetic in this bounded recipe.
            EXPECT_LE(error, 0.601);
            maximum_code_error = std::max(maximum_code_error, error);
          };
          auto x = expected->normal.x;
          auto y = expected->normal.y;
          const auto z = expected->normal.z;
          const auto norm = std::abs(x) + std::abs(y) + std::abs(z);
          x /= norm;
          y /= norm;
          if (z < 0) {
            const auto unfolded_x = std::copysign(1.0 - std::abs(y), x);
            y = std::copysign(1.0 - std::abs(x), y);
            x = unfolded_x;
          }
          check_code(words.at(0) & 1023U, (0.5 + (0.5 * x)) * 1023.0);
          check_code((words.at(0) >> 10U) & 1023U, (0.5 + (0.5 * y)) * 1023.0);
          check_code(words.at(1) & 255U, expected->material.metallic * 255.0);
          check_code((words.at(1) >> 8U) & 255U, 0.5 * 255.0);
          check_code((words.at(1) >> 16U) & 255U,
            expected->material.roughness.get() * 255.0);
          check_code(
            (words.at(2) >> 24U) & 255U, expected->ambient_occlusion * 255.0);
          const auto base = std::array {
            expected->material.base_color.red,
            expected->material.base_color.green,
            expected->material.base_color.blue,
          };
          for (unsigned channel = 0U; channel < base.size(); ++channel) {
            const auto linear = base.at(channel);
            const auto srgb = linear <= 0.0031308
              ? linear * 12.92
              : (1.055 * std::pow(linear, 1.0 / 2.4)) - 0.055;
            check_code((words.at(2) >> (channel * 8U)) & 255U, srgb * 255.0);
          }
          ++checked;
        }
      }
    }
    EXPECT_EQ(checked, 96U);
    EXPECT_EQ(rejected, 48U);
    RecordProperty("stored_material_cases", checked);
    RecordProperty("rejected_surface_cases", rejected);
    RecordProperty("maximum_stored_code_error", maximum_code_error);
  }

  NOLINT_TEST_F(
    MaterialRasterReferenceTest, FilteredFormatsMatchIndependentTexels)
  {
    struct AlphaCase {
      bool masked;
      float cutoff;
    };
    constexpr auto endpoints = std::array {
      Rgba8 { 1U, 65U, 129U, 129U },
      Rgba8 { 253U, 193U, 65U, 253U },
    };
    auto selectors = std::array<std::uint8_t, 16> {};
    auto pixels = std::array<Rgba8, 16> {};
    for (std::size_t pixel = 0U; pixel < pixels.size(); ++pixel) {
      const bool high = ((pixel % 4U) >= 2U) != ((pixel / 4U) >= 2U);
      selectors.at(pixel) = high ? 15U : 0U;
      pixels.at(pixel) = endpoints.at(high ? 1U : 0U);
    }
    constexpr auto normal_code = Rgba8 { 193U, 65U, 225U, 255U };
    auto normal_pixels = std::array<Rgba8, 16> {};
    normal_pixels.fill(normal_code);
    const auto normal_selectors = std::array<std::uint8_t, 16> {};
    auto normal_input = reference::MaterialEvaluationInput {};
    normal_input.samples.normal = reference::LinearRgb {
      .red = 193.0 / 255.0,
      .green = 65.0 / 255.0,
      .blue = 225.0 / 255.0,
    };
    const auto expected_normal = reference::EvaluateMaterial(normal_input);
    ASSERT_TRUE(expected_normal.has_value());
    std::array<std::shared_ptr<const graphics::Texture>, 2> surfaces;
    probe->inspect = [&](const auto&, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      surfaces = {
        owner->GetSceneTextures().GetGBufferResource(GBufferIndex::kNormal),
        owner->GetSceneTextures().GetGBufferResource(GBufferIndex::kBaseColor),
      };
    };
    unsigned checked = 0U;
    unsigned discarded = 0U;
    unsigned transfer_negative_controls = 0U;
    double maximum_emission_error = 0.0;
    for (const auto format : {
           Format::kRGBA8UNorm,
           Format::kRGBA8UNormSRGB,
           Format::kBC7UNorm,
           Format::kBC7UNormSRGB,
         }) {
      const bool srgb
        = format == Format::kRGBA8UNormSRGB || format == Format::kBC7UNormSRGB;
      const bool compressed
        = format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
      const auto color = AddEncodedTile(format, pixels, endpoints, selectors);
      const auto normal
        = AddEncodedTile(compressed ? Format::kBC7UNorm : Format::kRGBA8UNorm,
          normal_pixels, { normal_code, normal_code }, normal_selectors);
      for (const bool forward : { false, true }) {
        for (const auto alpha : {
               AlphaCase { .masked = false, .cutoff = 0.7F },
               AlphaCase { .masked = true, .cutoff = 0.3F },
               AlphaCase { .masked = true, .cutoff = 0.7F },
             }) {
          const bool masked = alpha.masked;
          for (const auto uv : std::array {
                 std::array { 0.125F, 0.125F },
                 std::array { 0.875F, 0.125F },
                 std::array { 0.5F, 0.25F },
                 std::array { 0.5F, 0.5F },
                 std::array { 0.0F, 0.25F },
                 std::array { 1.0F, 0.25F },
                 std::array { -0.125F, 0.25F },
                 std::array { 1.125F, 0.25F },
                 std::array { 0.375F, 0.375F },
                 std::array { 0.4375F, 0.4375F },
               }) {
            SCOPED_TRACE(static_cast<unsigned>(format));
            SCOPED_TRACE(forward);
            SCOPED_TRACE(masked);
            SCOPED_TRACE(uv.at(0));
            SCOPED_TRACE(uv.at(1));
            const double x = (static_cast<double>(uv.at(0)) * 4.0) - 0.5;
            const double y = (static_cast<double>(uv.at(1)) * 4.0) - 0.5;
            const auto ix = static_cast<int>(std::floor(x));
            const auto iy = static_cast<int>(std::floor(y));
            const auto fx = x - std::floor(x);
            const auto fy = y - std::floor(y);
            auto sample = std::array<double, 4> {};
            auto encoded_average = std::array<double, 4> {};
            for (int row = 0; row < 2; ++row) {
              for (int column = 0; column < 2; ++column) {
                const auto sx
                  = static_cast<std::size_t>((((ix + column) % 4) + 4) % 4);
                const auto sy
                  = static_cast<std::size_t>((((iy + row) % 4) + 4) % 4);
                const auto weight
                  = (column == 0 ? 1.0 - fx : fx) * (row == 0 ? 1.0 - fy : fy);
                for (std::size_t channel = 0U; channel < sample.size();
                  ++channel) {
                  const auto code = pixels.at((sy * 4U) + sx).at(channel);
                  const auto encoded = static_cast<double>(code) / 255.0;
                  sample.at(channel) += weight
                    * (srgb && channel < 3U ? reference::DecodeSrgb8(code)
                                            : encoded);
                  encoded_average.at(channel) += weight * encoded;
                }
              }
            }
            auto desc = data::pak::render::MaterialAssetDesc {};
            desc.material_domain = static_cast<std::uint8_t>(masked
                ? data::MaterialDomain::kMasked
                : data::MaterialDomain::kOpaque);
            desc.flags = data::pak::render::kMaterialFlag_DoubleSided;
            if (masked) {
              desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
            }
            for (auto& channel : desc.base_color) {
              channel = 1.0F;
            }
            for (auto& channel : desc.emissive_factor) {
              channel = data::HalfFloat { 2.0F };
            }
            desc.roughness = data::Unorm16 { 1.0F };
            desc.ambient_occlusion = data::Unorm16 { 1.0F };
            desc.normal_scale = 1.0F;
            desc.alpha_cutoff = data::Unorm16 { alpha.cutoff };
            desc.uv_offset[0] = uv.at(0) - 0.5F;
            desc.uv_offset[1] = uv.at(1) - 0.5F;
            auto keys = std::vector<content::ResourceKey>(
              static_cast<std::size_t>(MaterialSlot::kCount));
            keys.at(static_cast<std::size_t>(MaterialSlot::kBaseColor)) = color;
            keys.at(static_cast<std::size_t>(MaterialSlot::kNormal)) = normal;
            keys.at(static_cast<std::size_t>(MaterialSlot::kEmissive)) = color;
            mesh_node.GetRenderable().SetMaterialOverride(0U, 0U,
              std::make_shared<data::MaterialAsset>(
                data::AssetKey::FromVirtualPath("/Test/Lighting/Filtered-"
                  + std::to_string(++material_sequence) + ".omat"),
                desc, std::vector<data::ShaderReference> {}, std::move(keys)));
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
            const auto image = ReadFloatTexture(*probe->color);
            ASSERT_EQ(image.size(), 1U);
            const bool visible = !masked
              || sample.at(3)
                >= static_cast<double>(desc.alpha_cutoff.get()) / 65535.0;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
              const auto expected = visible ? sample.at(channel) * 2.0 : 0.0;
              const auto error = std::abs(image.front().at(channel) - expected);
              EXPECT_LE(error, (0.02 * expected) + 2.0e-5);
              maximum_emission_error = std::max(maximum_emission_error, error);
              if (srgb && visible) {
                const auto encoded = encoded_average.at(channel);
                const auto wrong_order = encoded <= 0.04045
                  ? encoded / 12.92
                  : std::pow((encoded + 0.055) / 1.055, 2.4);
                if (std::abs((wrong_order * 2.0) - expected)
                  > (0.02 * expected) + 2.0e-5) {
                  ++transfer_negative_controls;
                }
              }
            }
            if (!visible) {
              ++discarded;
            }
            if (!forward && visible) {
              auto words = std::array<std::uint32_t, 2> {};
              for (std::size_t index = 0U; index < surfaces.size(); ++index) {
                ASSERT_NE(surfaces.at(index), nullptr);
                ASSERT_NO_FATAL_FAILURE(
                  ReadPacked(*surfaces.at(index), words.at(index)));
              }
              const auto& n = expected_normal->normal;
              const auto norm = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
              EXPECT_NEAR(words.at(0) & 1023U,
                (0.5 + (0.5 * n.x / norm)) * 1023.0, 0.601);
              EXPECT_NEAR((words.at(0) >> 10U) & 1023U,
                (0.5 + (0.5 * n.y / norm)) * 1023.0, 0.601);
              for (unsigned channel = 0U; channel < 3U; ++channel) {
                const auto linear = sample.at(channel);
                const auto encoded = linear <= 0.0031308
                  ? linear * 12.92
                  : (1.055 * std::pow(linear, 1.0 / 2.4)) - 0.055;
                // sRGB input decoding and output encoding each have a format
                // error allowance; linear input only needs the output
                // allowance.
                EXPECT_NEAR((words.at(1) >> (8U * channel)) & 255U,
                  encoded * 255.0, srgb ? 1.101 : 0.601);
              }
            }
            ++checked;
          }
        }
      }
    }
    EXPECT_EQ(checked, 240U);
    EXPECT_GT(discarded, 0U);
    EXPECT_GT(transfer_negative_controls, 0U);
    RecordProperty("filtered_material_cases", checked);
    RecordProperty("masked_discards", discarded);
    RecordProperty(
      "decode_after_filter_rejections", transfer_negative_controls);
    RecordProperty("maximum_emission_absolute_error", maximum_emission_error);
  }
  NOLINT_TEST_F(
    MaterialRasterReferenceTest, MipFilteringMatchesIndependentLevelBlend)
  {
    auto vertices = std::vector<data::Vertex>(3U);
    const auto positions = std::array {
      glm::vec3 { -2, -2, -1 },
      glm::vec3 { 2, -2, -1 },
      glm::vec3 { 0, 2, -1 },
    };
    for (std::size_t index = 0U; index < vertices.size(); ++index) {
      const auto p = positions.at(index);
      vertices.at(index) = {
        .position = p,
        .normal = { 0, 0, 1 },
        .texcoord = { (p.x + 2.0F) * 0.25F, (p.y + 2.0F) * 0.25F },
        .tangent = { 1, 0, 0 },
        .bitangent = { 0, 1, 0 },
        .color = { 1, 1, 1, 1 },
      };
    }
    std::shared_ptr<data::Mesh> mesh
      = data::MeshBuilder()
          .WithVertices(vertices)
          .WithIndices(std::vector<std::uint32_t> { 0U, 1U, 2U })
          .BeginSubMesh("Mip receiver", data::MaterialAsset::CreateDefault())
          .WithMeshView({
            .first_index = 0U,
            .index_count = 3U,
            .first_vertex = 0U,
            .vertex_count = 3U,
          })
          .EndSubMesh()
          .Build();
    auto geometry = data::pak::geometry::GeometryAssetDesc {};
    geometry.lod_count = 1U;
    geometry.bounding_box_min[0] = geometry.bounding_box_min[1] = -2.0F;
    geometry.bounding_box_max[0] = geometry.bounding_box_max[1] = 2.0F;
    geometry.bounding_box_min[2] = geometry.bounding_box_max[2] = -1.0F;
    mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Test/Lighting/MipReceiver.ogeo"),
      geometry, std::vector<std::shared_ptr<data::Mesh>> { mesh }));
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens.has_value());
    lens->get().SetFieldOfView(std::numbers::pi_v<float> / 2.0F);
    lens->get().SetAspectRatio(1.0F);
    constexpr auto colors = std::array {
      Rgba8 { 65U, 129U, 193U, 255U },
      Rgba8 { 193U, 65U, 129U, 255U },
      Rgba8 { 129U, 193U, 65U, 255U },
    };
    unsigned checked = 0U;
    double maximum_error = 0.0;
    for (const auto format : {
           Format::kRGBA8UNorm,
           Format::kRGBA8UNormSRGB,
           Format::kBC7UNorm,
           Format::kBC7UNormSRGB,
         }) {
      const bool srgb
        = format == Format::kRGBA8UNormSRGB || format == Format::kBC7UNormSRGB;
      const bool compressed
        = format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
      auto desc = data::pak::core::TextureResourceDesc {};
      desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
      desc.width = desc.height = 4U;
      desc.depth = desc.array_layers = 1U;
      desc.mip_levels = 3U;
      desc.format = static_cast<std::uint8_t>(format);
      desc.alignment = 256U;
      desc.compression_type = compressed ? 7U : 0U;
      auto bytes = std::vector<std::uint8_t> {};
      auto layouts = std::array<data::pak::render::SubresourceLayout, 3> {};
      for (std::size_t level = 0U; level < colors.size(); ++level) {
        const auto width = 4U >> level;
        const auto size = compressed ? 16U : width * width * 4U;
        const auto offset = bytes.size();
        bytes.resize(offset + size);
        if (compressed) {
          const auto block
            = EncodeBc7({ colors.at(level), colors.at(level) }, {});
          std::ranges::copy(
            block, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
        } else {
          for (std::size_t pixel = 0U;
            pixel < static_cast<std::size_t>(width) * width; ++pixel) {
            const auto destination
              = std::span(bytes).subspan(offset + (pixel * 4U), 4U);
            std::ranges::copy(colors.at(level), destination.begin());
          }
        }
        layouts.at(level) = {
          .offset_bytes = static_cast<std::uint32_t>(offset),
          .row_pitch_bytes
          = static_cast<std::uint32_t>(compressed ? 16U : width * 4U),
          .size_bytes = static_cast<std::uint32_t>(size),
        };
      }
      const auto key = AddTexture(desc, layouts, bytes);
      for (const bool forward : { false, true }) {
        for (const auto scale : {
               0.25F,
               0.5F,
               std::sqrt(0.5F),
               1.0F,
               std::numbers::sqrt2_v<float>,
               2.0F,
               4.0F,
             }) {
          SCOPED_TRACE(static_cast<unsigned>(format));
          SCOPED_TRACE(forward);
          SCOPED_TRACE(scale);
          auto material = data::pak::render::MaterialAssetDesc {};
          material.material_domain
            = static_cast<std::uint8_t>(data::MaterialDomain::kOpaque);
          material.flags = data::pak::render::kMaterialFlag_DoubleSided;
          for (auto& channel : material.base_color) {
            channel = 1.0F;
          }
          for (auto& channel : material.emissive_factor) {
            channel = data::HalfFloat { 1.0F };
          }
          material.roughness = data::Unorm16 { 1.0F };
          material.ambient_occlusion = data::Unorm16 { 1.0F };
          material.normal_scale = 1.0F;
          material.uv_scale[0] = material.uv_scale[1] = scale;
          auto keys = std::vector<content::ResourceKey>(
            static_cast<std::size_t>(MaterialSlot::kCount));
          keys.at(static_cast<std::size_t>(MaterialSlot::kBaseColor)) = key;
          keys.at(static_cast<std::size_t>(MaterialSlot::kEmissive)) = key;
          mesh_node.GetRenderable().SetMaterialOverride(0U, 0U,
            std::make_shared<data::MaterialAsset>(
              data::AssetKey::FromVirtualPath("/Test/Lighting/Mip-"
                + std::to_string(++material_sequence) + ".omat"),
              material, std::vector<data::ShaderReference> {},
              std::move(keys)));
          // At 90-degree FOV on a unit viewport, d(world.xy)/d(pixel)=2.
          // The authored UV gradient is 1/4 and texture width is 4:
          // rho=2*scale.
          const auto lod = std::clamp(std::log2(2.0 * scale), 0.0, 2.0);
          const auto low = static_cast<std::size_t>(std::floor(lod));
          const auto high = std::min(low + 1U, colors.size() - 1U);
          const auto blend = lod - std::floor(lod);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
          const auto image = ReadFloatTexture(*probe->color);
          ASSERT_EQ(image.size(), 1U);
          for (std::size_t channel = 0U; channel < 3U; ++channel) {
            const auto decode = [&](std::uint8_t code) -> double {
              return srgb ? reference::DecodeSrgb8(code)
                          : static_cast<double>(code) / 255.0;
            };
            const auto expected
              = ((1.0 - blend) * decode(colors.at(low).at(channel)))
              + (blend * decode(colors.at(high).at(channel)));
            const auto error = std::abs(image.front().at(channel) - expected);
            EXPECT_LE(error, (0.02 * expected) + 2.0e-5);
            maximum_error = std::max(maximum_error, error);
          }
          ++checked;
        }
      }
    }
    EXPECT_EQ(checked, 56U);
    RecordProperty("mip_material_cases", checked);
    RecordProperty("maximum_mip_absolute_error", maximum_error);
  }
} // namespace
} // namespace oxygen::vortex::testing
