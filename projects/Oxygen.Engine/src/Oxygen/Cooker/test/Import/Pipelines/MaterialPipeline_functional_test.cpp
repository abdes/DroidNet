//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Pipelines/MaterialPipeline.cpp

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/test/Support/CookedRecords.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/PipelineHarness.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureSampling.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MaterialTextureSlot.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import {
namespace {

  using oxygen::cooker::test::RunPipelineOnce;

  //=== Test Helpers
  //===---------------------------------------------------------//

  auto MakeShaderRequest(ShaderType stage, std::string source_path,
    std::string entry_point, std::string defines = {}) -> ShaderRequest
  {
    return ShaderRequest {
      .shader_type = static_cast<uint8_t>(stage),
      .source_path = std::move(source_path),
      .entry_point = std::move(entry_point),
      .defines = std::move(defines),
      .shader_hash = 0,
    };
  }

  auto MakeRequest() -> ImportRequest
  {
    ImportRequest request;
    request.source_path = "Test.fbx";
    return request;
  }

  auto MakeBaseItem() -> MaterialPipeline::WorkItem
  {
    MaterialPipeline::WorkItem item;
    item.source_id = "mat0";
    item.material.name = "Material_0";
    item.material.storage_name = "Material_0";
    item.request = MakeRequest();
    item.material.shader_requests = {
      MakeShaderRequest(ShaderType::kVertex,
        "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl", "VS"),
      MakeShaderRequest(ShaderType::kPixel,
        "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl", "PS"),
    };
    return item;
  }

  using oxygen::cooker::test::ReadMaterialDesc;

  auto ReadShaderRefs(const std::vector<std::byte>& bytes, size_t count)
    -> std::vector<data::pak::render::ShaderReferenceDesc>
  {
    using data::pak::render::ShaderReferenceDesc;
    std::vector<ShaderReferenceDesc> refs;
    for (size_t i = 0; i < count; ++i) {
      const auto ref = serio::CopyAt<ShaderReferenceDesc>(bytes,
        serio::ByteOffset { sizeof(data::pak::render::MaterialAssetDesc)
          + (i * sizeof(ShaderReferenceDesc)) });
      if (!ref) {
        return {};
      }
      refs.push_back(*ref);
    }
    return refs;
  }

  using oxygen::cooker::test::HasDiagnosticCode;

  auto ExpectedShaderStages(const std::vector<ShaderRequest>& requests)
    -> uint32_t
  {
    uint32_t stages = 0;
    for (const auto& request : requests) {
      const uint32_t bit = 1U << request.shader_type;
      stages |= bit;
    }
    return stages;
  }

  //=== Fixtures
  //===--------------------------------------------------------------//

  class MaterialPipelineTest : public testing::Test {
  protected:
    ImportEventLoop loop_;
    co::ThreadPool pool_ { loop_, 2 };
  };

  //=== Basic Behavior Tests
  //===-----------------------------------------------------//

  NOLINT_TEST_F(MaterialPipelineTest, RejectsInvalidCompiledEmission)
  {
    co::Run(loop_, [&] -> co::Co<> {
      MaterialPipeline pipeline(pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });
      OXCO_WITH_NURSERY(n)
      {
        pipeline.Start(n);
        for (const auto value : std::array {
               -1.0F,
               65505.0F,
               std::numeric_limits<float>::infinity(),
               std::numeric_limits<float>::quiet_NaN(),
             }) {
          auto item = MakeBaseItem();
          item.material.inputs.emissive_factor[0] = value;
          co_await pipeline.Submit(std::move(item));
          const auto result = co_await pipeline.Collect();
          EXPECT_FALSE(result.success);
          EXPECT_FALSE(result.cooked.has_value());
          EXPECT_TRUE(HasDiagnosticCode(
            result.diagnostics, import_codes::kMaterialEmissiveFactorRange));
        }
        pipeline.Close();
        co_return co::kJoin;
      };
    });
  }

  //=== ORM Policy Tests
  //===----------------------------------------------------------//

  NOLINT_TEST_F(MaterialPipelineTest, CollectAutoOrmPackedSetsFlags)
  {
    for (const auto ao_index : std::array<uint32_t, 3> {
           data::pak::core::kNoResourceIndex.get(),
           7U,
           9U,
         }) {
      auto item = MakeBaseItem();
      item.material.orm_policy = OrmPolicy::kAuto;
      item.material.textures.metallic = MaterialTextureBinding {
        .index = 7,
        .assigned = true,
        .source_id = "orm",
        .uv_set = 0,
        .uv_transform = {},
      };
      item.material.textures.roughness = item.material.textures.metallic;
      if (ao_index != data::pak::core::kNoResourceIndex) {
        item.material.textures.ambient_occlusion
          = item.material.textures.metallic;
        item.material.textures.ambient_occlusion.index = ao_index;
        item.material.textures.ambient_occlusion.source_id
          = ao_index == 7U ? "orm" : "ao";
      }
      item.material.occlusion_mode = AmbientOcclusionMode::kStrength;
      item.material.inputs.ambient_occlusion = 0.5F;

      const auto result
        = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
          MaterialPipeline::Config {
            .queue_capacity = 4,
            .worker_count = 1,
            .use_thread_pool = true,
          });

      ASSERT_TRUE(result.success);
      ASSERT_HAS_VALUE(result.cooked)
        << "Expected result.cooked to contain a value";
      const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);

      EXPECT_NE(
        desc.flags & data::pak::render::kMaterialFlag_GltfOrmPacked, 0U);
      EXPECT_EQ(
        desc.flags & data::pak::render::kMaterialFlag_NoTextureSampling, 0U);
      const auto& references = result.cooked->references;
      for (const auto reference :
        { desc.metallic_texture, desc.roughness_texture }) {
        const auto resolved
          = references.ResolveResource(reference, data::ResourceKind::kTexture);
        ASSERT_HAS_VALUE(resolved);
        EXPECT_EQ(*resolved, std::optional { oxygen::ResourceIndexT { 7U } });
      }
      const auto resolved_ao = references.ResolveResource(
        desc.ambient_occlusion_texture, data::ResourceKind::kTexture);
      ASSERT_HAS_VALUE(resolved_ao);
      if (ao_index == data::pak::core::kNoResourceIndex.get()) {
        EXPECT_FALSE(resolved_ao->has_value());
      } else {
        EXPECT_EQ(
          *resolved_ao, std::optional { oxygen::ResourceIndexT { ao_index } });
      }
      EXPECT_NE(
        desc.flags & data::pak::render::kMaterialFlag_AmbientOcclusionStrength,
        0U);
      EXPECT_EQ(desc.ambient_occlusion.get(), data::Unorm16 { 0.5F }.get());
    }
  }

  NOLINT_TEST_F(MaterialPipelineTest, CollectForcePackedInvalidEmitsError)
  {
    auto item = MakeBaseItem();
    item.material.orm_policy = OrmPolicy::kForcePacked;
    item.material.textures.metallic = MaterialTextureBinding {
      .index = 4,
      .assigned = true,
      .source_id = "metal",
      .uv_set = 0,
      .uv_transform = {},
    };
    item.material.textures.roughness = MaterialTextureBinding {
      .index = 5,
      .assigned = true,
      .source_id = "rough",
      .uv_set = 0,
      .uv_transform = {},
    };
    item.material.textures.ambient_occlusion = item.material.textures.metallic;

    const auto result
      = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(
      HasDiagnosticCode(result.diagnostics, import_codes::kMaterialOrmPolicy));
  }

  //=== UV Transform Tests
  //===---------------------------------------------------------//

  //! Each slot keeps its own UV set, transform and sampler; a slot with no
  //! texture holds the defaults.
  NOLINT_TEST_F(MaterialPipelineTest, EachSlotKeepsItsOwnSampling)
  {
    auto item = MakeBaseItem();
    item.material.textures.base_color = MaterialTextureBinding {
      .index = 2,
      .assigned = true,
      .source_id = "base",
      .uv_set = 1,
      .uv_transform = { .scale = { 2.0F, 3.0F },
        .offset = { 0.25F, 0.5F },
        .rotation_radians = 0.1F, },
      .sampler = {
        .address_u = TextureAddress::kClamp,
        .address_v = TextureAddress::kMirror,
        .mag_filter = TextureFilter::kNearest,
        .min_filter = TextureFilter::kNearest,
        .mip_filter = TextureMipFilter::kNone,
      },
    };
    item.material.textures.normal = MaterialTextureBinding {
      .index = 3,
      .assigned = true,
      .source_id = "normal",
      .uv_set = 0,
      .uv_transform = { .scale = { 4.0F, 4.0F } },
    };

    const auto result
      = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";
    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);

    const auto slot_of
      = [&desc](const data::MaterialTextureSlot slot) -> auto& {
      return desc.texture_sampling[static_cast<std::size_t>(slot)];
    };
    const auto& base = slot_of(data::MaterialTextureSlot::kBaseColor);
    EXPECT_EQ(base.uv_set, 1U);
    EXPECT_FLOAT_EQ(base.uv_scale[0], 2.0F);
    EXPECT_FLOAT_EQ(base.uv_scale[1], 3.0F);
    EXPECT_FLOAT_EQ(base.uv_offset[0], 0.25F);
    EXPECT_FLOAT_EQ(base.uv_offset[1], 0.5F);
    EXPECT_FLOAT_EQ(base.uv_rotation_radians, 0.1F);
    EXPECT_EQ(base.address_u, static_cast<uint8_t>(TextureAddress::kClamp));
    EXPECT_EQ(base.address_v, static_cast<uint8_t>(TextureAddress::kMirror));
    EXPECT_EQ(base.mag_filter, static_cast<uint8_t>(TextureFilter::kNearest));
    EXPECT_EQ(base.min_filter, static_cast<uint8_t>(TextureFilter::kNearest));
    EXPECT_EQ(base.mip_filter, static_cast<uint8_t>(TextureMipFilter::kNone));

    const auto& normal = slot_of(data::MaterialTextureSlot::kNormal);
    EXPECT_EQ(normal.uv_set, 0U);
    EXPECT_FLOAT_EQ(normal.uv_scale[0], 4.0F);
    EXPECT_EQ(normal.address_u, static_cast<uint8_t>(TextureAddress::kRepeat));
    EXPECT_EQ(
      normal.mip_filter, static_cast<uint8_t>(TextureMipFilter::kLinear));

    const auto& thickness = slot_of(data::MaterialTextureSlot::kThickness);
    EXPECT_FLOAT_EQ(thickness.uv_scale[0], 1.0F);
    EXPECT_EQ(
      thickness.mip_filter, static_cast<uint8_t>(TextureMipFilter::kLinear));
  }

  //! Sampling belongs to the reference: one image read two ways is one texture.
  NOLINT_TEST_F(MaterialPipelineTest, OneImageUnderTwoSamplersIsOneTexture)
  {
    auto item = MakeBaseItem();
    item.material.textures.base_color = MaterialTextureBinding {
      .index = 2,
      .assigned = true,
      .source_id = "shared",
    };
    item.material.textures.emissive = MaterialTextureBinding {
      .index = 2,
      .assigned = true,
      .source_id = "shared",
      .sampler = { .address_u = TextureAddress::kClamp },
    };

    const auto result
      = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";
    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
    EXPECT_EQ(desc.base_color_texture, desc.emissive_texture);
  }

  //=== Authored Value Tests
  //===---------------------------------------------------//

  class MaterialPipelineValuesTest : public MaterialPipelineTest {
  protected:
    auto Cook(MaterialPipeline::WorkItem item) -> MaterialPipeline::WorkResult
    {
      return RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });
    }
  };

  //! Cutoff 0 keeps every pixel: the value authored is the value the renderer
  //! tests against.
  NOLINT_TEST_F(MaterialPipelineValuesTest, MaskedCutoffZeroStaysZero)
  {
    auto item = MakeBaseItem();
    item.material.alpha_mode = MaterialAlphaMode::kMasked;
    item.material.inputs.alpha_cutoff = 0.0F;

    const auto result = Cook(std::move(item));

    ASSERT_HAS_VALUE(result.cooked);
    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
    EXPECT_EQ(desc.alpha_cutoff.get(), 0U);
    EXPECT_FALSE(HasDiagnosticCode(
      result.diagnostics, import_codes::kMaterialAlphaCutoffRange));
  }

  //! Glossiness stays a roughness in the descriptor; only a texture needs the
  //! renderer to know, because its samples read the other way.
  NOLINT_TEST_F(MaterialPipelineValuesTest, GlossinessFlagFollowsTheTexture)
  {
    using data::pak::render::kMaterialFlag_GlossinessTexture;
    auto scalar = MakeBaseItem();
    scalar.material.inputs.roughness_as_glossiness = true;
    scalar.material.inputs.roughness = 0.25F;
    auto textured = scalar;
    textured.material.textures.roughness = MaterialTextureBinding {
      .index = 2,
      .assigned = true,
      .source_id = "gloss",
    };

    const auto without = Cook(std::move(scalar));
    const auto with = Cook(std::move(textured));

    ASSERT_HAS_VALUE(without.cooked);
    ASSERT_HAS_VALUE(with.cooked);
    const auto scalar_desc = ReadMaterialDesc(without.cooked->descriptor_bytes);
    const auto textured_desc = ReadMaterialDesc(with.cooked->descriptor_bytes);
    EXPECT_EQ(scalar_desc.flags & kMaterialFlag_GlossinessTexture, 0U);
    EXPECT_NE(textured_desc.flags & kMaterialFlag_GlossinessTexture, 0U);
    constexpr auto kTolerance = 0.001F;
    EXPECT_NEAR(scalar_desc.roughness.ToFloat(), 0.75F, kTolerance);
    EXPECT_NEAR(textured_desc.roughness.ToFloat(), 0.75F, kTolerance);
  }

  NOLINT_TEST_F(MaterialPipelineValuesTest, ProceduralGridReachesTheDescriptor)
  {
    auto item = MakeBaseItem();
    item.material.procedural_grid = MaterialProceduralGrid {
      .spacing = { 2.0F, 3.0F },
      .major_every = 5,
      .fade_start = 10.0F,
      .fade_end = 20.0F,
      .origin_color = { 0.1F, 0.2F, 0.3F, 0.4F },
    };

    const auto result = Cook(std::move(item));

    ASSERT_HAS_VALUE(result.cooked);
    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
    EXPECT_NE(desc.flags & data::pak::render::kMaterialFlag_ProceduralGrid, 0U);
    EXPECT_FLOAT_EQ(desc.grid_spacing[1], 3.0F);
    EXPECT_EQ(desc.grid_major_every, 5U);
    EXPECT_FLOAT_EQ(desc.grid_fade_start, 10.0F);
    EXPECT_FLOAT_EQ(desc.grid_fade_end, 20.0F);
    EXPECT_FLOAT_EQ(desc.grid_origin_color[3], 0.4F);
    // What it does not say keeps the format's defaults.
    EXPECT_FLOAT_EQ(desc.grid_major_thickness, 0.04F);
  }

  //! A material's domain and alpha mode cook to one answer, and say so when
  //! they disagree.
  NOLINT_TEST_F(MaterialPipelineValuesTest, DomainAndAlphaModeCookToOneAnswer)
  {
    using data::MaterialDomain;
    using data::pak::render::kMaterialFlag_AlphaTest;
    struct Case {
      MaterialDomain domain;
      MaterialAlphaMode mode;
      MaterialDomain cooked;
      bool alpha_test;
      bool warns;
    };
    for (const auto& [domain, mode, cooked, alpha_test, warns] : std::array {
           Case {
             .domain = MaterialDomain::kOpaque,
             .mode = MaterialAlphaMode::kOpaque,
             .cooked = MaterialDomain::kOpaque,
             .alpha_test = false,
             .warns = false,
           },
           Case {
             .domain = MaterialDomain::kOpaque,
             .mode = MaterialAlphaMode::kMasked,
             .cooked = MaterialDomain::kMasked,
             .alpha_test = true,
             .warns = false,
           },
           Case {
             .domain = MaterialDomain::kOpaque,
             .mode = MaterialAlphaMode::kBlended,
             .cooked = MaterialDomain::kAlphaBlended,
             .alpha_test = false,
             .warns = false,
           },
           Case {
             .domain = MaterialDomain::kMasked,
             .mode = MaterialAlphaMode::kMasked,
             .cooked = MaterialDomain::kMasked,
             .alpha_test = true,
             .warns = false,
           },
           Case {
             .domain = MaterialDomain::kMasked,
             .mode = MaterialAlphaMode::kOpaque,
             .cooked = MaterialDomain::kMasked,
             .alpha_test = true,
             .warns = true,
           },
           Case {
             .domain = MaterialDomain::kMasked,
             .mode = MaterialAlphaMode::kBlended,
             .cooked = MaterialDomain::kAlphaBlended,
             .alpha_test = false,
             .warns = true,
           },
           Case {
             .domain = MaterialDomain::kAlphaBlended,
             .mode = MaterialAlphaMode::kOpaque,
             .cooked = MaterialDomain::kAlphaBlended,
             .alpha_test = false,
             .warns = true,
           },
           Case {
             .domain = MaterialDomain::kAlphaBlended,
             .mode = MaterialAlphaMode::kMasked,
             .cooked = MaterialDomain::kMasked,
             .alpha_test = true,
             .warns = true,
           },
           Case {
             .domain = MaterialDomain::kDecal,
             .mode = MaterialAlphaMode::kMasked,
             .cooked = MaterialDomain::kDecal,
             .alpha_test = true,
             .warns = false,
           },
           Case {
             .domain = MaterialDomain::kDecal,
             .mode = MaterialAlphaMode::kBlended,
             .cooked = MaterialDomain::kDecal,
             .alpha_test = false,
             .warns = false,
           },
         }) {
      auto item = MakeBaseItem();
      item.material.domain = domain;
      item.material.alpha_mode = mode;

      const auto result = Cook(std::move(item));

      ASSERT_HAS_VALUE(result.cooked);
      const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
      EXPECT_EQ(desc.material_domain, static_cast<uint8_t>(cooked));
      EXPECT_EQ((desc.flags & kMaterialFlag_AlphaTest) != 0U, alpha_test);
      EXPECT_EQ(HasDiagnosticCode(result.diagnostics,
                  import_codes::kMaterialDomainAlphaConflict),
        warns);
    }
  }

  //=== Shader Reference Tests
  //===----------------------------------------------------//

  NOLINT_TEST_F(MaterialPipelineTest, CollectShaderStagesOrderedByBitIndex)
  {
    auto item = MakeBaseItem();
    item.material.shader_requests = {
      MakeShaderRequest(ShaderType::kPixel,
        "Vortex/Stages/Translucency/ForwardMesh_PS.hlsl", "PS"),
      MakeShaderRequest(ShaderType::kVertex,
        "Vortex/Stages/Translucency/ForwardMesh_VS.hlsl", "VS"),
    };
    const auto expected_stages
      = ExpectedShaderStages(item.material.shader_requests);

    const auto result
      = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";

    const auto desc = ReadMaterialDesc(result.cooked->descriptor_bytes);
    EXPECT_EQ(desc.shader_stages, expected_stages);

    const auto ref_count = std::popcount(desc.shader_stages);
    const auto refs
      = ReadShaderRefs(result.cooked->descriptor_bytes, ref_count);
    ASSERT_EQ(refs.size(), ref_count);
    EXPECT_EQ(
      refs.at(0).shader_type, static_cast<uint8_t>(ShaderType::kVertex));
    EXPECT_EQ(refs.at(1).shader_type, static_cast<uint8_t>(ShaderType::kPixel));
  }

  NOLINT_TEST_F(MaterialPipelineTest, CollectLongShaderStringsStoredWhole)
  {
    // Longer than the former 63-byte name fields, and not ASCII.
    const auto source_path
      = "Shaders/\xC3\x89" + std::string(200, 's') + ".hlsl";
    const auto entry_point = std::string(80, 'e');
    const auto defines = "D" + std::string(299, '_');
    auto item = MakeBaseItem();
    item.material.shader_requests = {
      MakeShaderRequest(ShaderType::kVertex, source_path, entry_point, defines),
    };

    const auto result
      = RunPipelineOnce<MaterialPipeline>(loop_, std::move(item), pool_,
        MaterialPipeline::Config {
          .queue_capacity = 4,
          .worker_count = 1,
          .use_thread_pool = true,
        });

    ASSERT_TRUE(result.success);
    ASSERT_HAS_VALUE(result.cooked)
      << "Expected result.cooked to contain a value";
    EXPECT_TRUE(result.diagnostics.empty());
    const auto& bytes = result.cooked->descriptor_bytes;
    const auto refs = ReadShaderRefs(bytes, 1U);
    ASSERT_EQ(refs.size(), 1U);
    EXPECT_EQ(oxygen::cooker::testing::ReadDescriptorString(
                bytes, refs.at(0).source_path_offset),
      source_path);
    EXPECT_EQ(oxygen::cooker::testing::ReadDescriptorString(
                bytes, refs.at(0).entry_point_offset),
      entry_point);
    EXPECT_EQ(oxygen::cooker::testing::ReadDescriptorString(
                bytes, refs.at(0).defines_offset),
      defines);
  }

} // namespace
} // namespace oxygen::content::import
