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
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Texture.h>
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
    auto AddTexture(const exposure::Pixel& pixel) -> content::ResourceKey
    {
      auto desc = data::pak::core::TextureResourceDesc {};
      desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
      desc.width = desc.height = desc.depth = desc.mip_levels
        = desc.array_layers = 1U;
      desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
      constexpr std::uint32_t alignment = 256U;
      desc.alignment = alignment;
      const auto key = owned_asset_loader_->MintSyntheticTextureKey();
      desc.content_hash = key.get();
      auto bytes = std::vector<std::uint8_t>(sizeof(pixel));
      std::memcpy(bytes.data(), pixel.data(), sizeof(pixel));
      const auto layouts = std::array {
        data::pak::render::SubresourceLayout {
          .offset_bytes = 0U,
          .row_pitch_bytes = sizeof(pixel),
          .size_bytes = sizeof(pixel),
        },
      };
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
} // namespace
} // namespace oxygen::vortex::testing
