//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::vortex::testing::exposure {
namespace {

  // Independent face basis; hardware cube Y is Oxygen world Z.
  auto WorldDirection(unsigned face, unsigned x, unsigned y, unsigned size)
    -> glm::vec3
  {
    const auto u = 2.0F * (x + 0.5F) / size - 1.0F;
    const auto v = 2.0F * (y + 0.5F) / size - 1.0F;
    const std::array directions { glm::vec3 { 1, -v, -u },
      glm::vec3 { -1, -v, u }, glm::vec3 { u, 1, v }, glm::vec3 { u, -1, -v },
      glm::vec3 { u, -v, 1 }, glm::vec3 { -u, -v, -1 } };
    const auto cube = glm::normalize(directions.at(face));
    return { cube.x, -cube.z, cube.y };
  }

  // Native shader uses a clamped linear lookup. NoV=1 selects the last column;
  // interpolate the immutable U16 source texels independently on the CPU.
  auto BrdfAtNormalIncidence(float roughness) -> std::array<double, 2>
  {
    namespace ibl = environment::internal;
    const auto data = ibl::GetIblBrdfLookup();
    const double y = roughness * ibl::kIblBrdfHeight - 0.5;
    const int low = static_cast<int>(std::floor(y));
    const double fraction = y - low;
    std::array<double, 2> result {};
    for (unsigned c = 0; c < 2; ++c) {
      const auto at = [&](int row) {
        const auto index = std::clamp(row, 0, int(ibl::kIblBrdfHeight) - 1)
            * ibl::kIblBrdfWidth
          + ibl::kIblBrdfWidth - 1;
        return double(data[index][c]) / 65535.0;
      };
      result[c] = at(low) * (1.0 - fraction) + at(low + 1) * fraction;
    }
    return result;
  }

  class IblSurfaceGpuTest : public ExposureLightingGpuTest {
  protected:
    auto SetUp() -> void override
    {
      ExposureLightingGpuTest::SetUp();
      auto& sky
        = scene->GetEnvironment()->AddSystem<scene::environment::SkyLight>();
      sky.SetEnabled(true);
      sky.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
      sky.SetLowerHemisphereIsSolidColor(false);
    }

    auto Sky() -> scene::environment::SkyLight&
    {
      return *scene->GetEnvironment()
                ->TryGetSystem<scene::environment::SkyLight>();
    }

    auto WriteComparisonMetadata(
      const environment::internal::IblGpuProducts& products,
      const environment::IblProductMetadata& metadata) -> void
    {
      // Test-only format comparison between completed frames. Preserve the
      // products and generation; only the half-selection flag is overridden.
      auto upload = CreateUploadBuffer(SizeBytes { sizeof(metadata) });
      upload->Update(&metadata, sizeof(metadata), 0U);
      SubmitCommands(
        "IBL precision comparison", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
          EnsureTracked(
            recorder, upload, graphics::ResourceStates::kGenericRead);
          recorder.RequireResourceState(
            *products.metadata, graphics::ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBuffer(
            *products.metadata, 0U, *upload, 0U, sizeof(metadata));
          recorder.RequireResourceStateFinal(
            *products.metadata, graphics::ResourceStates::kShaderResource);
        });
      WaitForQueueIdle();
    }

    auto Texture(unsigned size, bool cube,
      const std::function<Pixel(unsigned, unsigned, unsigned)>& pixel)
      -> content::ResourceKey
    {
      const auto faces = cube ? 6U : 1U;
      data::pak::core::TextureResourceDesc desc {};
      desc.texture_type = static_cast<std::uint8_t>(
        cube ? TextureType::kTextureCube : TextureType::kTexture2D);
      desc.width = desc.height = size;
      desc.depth = desc.mip_levels = 1U;
      desc.array_layers = static_cast<std::uint16_t>(faces);
      desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
      desc.alignment = 256U;
      const auto key = owned_asset_loader_->MintSyntheticTextureKey();
      desc.content_hash = key.get();
      const auto face_bytes = size * size * sizeof(Pixel);
      std::vector<std::uint8_t> bytes(faces * face_bytes);
      std::vector<data::pak::render::SubresourceLayout> layouts;
      for (unsigned face = 0; face < faces; ++face) {
        for (unsigned y = 0; y < size; ++y) {
          for (unsigned x = 0; x < size; ++x) {
            const auto value = pixel(face, x, y);
            std::memcpy(
              bytes.data() + face * face_bytes + (y * size + x) * sizeof(Pixel),
              value.data(), sizeof(Pixel));
          }
        }
        layouts.push_back(
          { .offset_bytes = static_cast<std::uint32_t>(face * face_bytes),
            .row_pitch_bytes = static_cast<std::uint32_t>(size * sizeof(Pixel)),
            .size_bytes = static_cast<std::uint32_t>(face_bytes) });
      }
      auto payload
        = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
      desc.size_bytes = static_cast<std::uint32_t>(payload.size());
      owned_asset_loader_->SetTexture(
        key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
      return key;
    }

    auto Material(data::MaterialDomain domain, float roughness, float metallic,
      const glm::vec3& color, content::ResourceKey normal = {}) -> void
    {
      auto desc = data::pak::render::MaterialAssetDesc {};
      desc.material_domain = static_cast<std::uint8_t>(domain);
      desc.flags = data::pak::render::kMaterialFlag_DoubleSided;
      if (normal.get() == 0U)
        desc.flags |= data::pak::render::kMaterialFlag_NoTextureSampling;
      if (domain == data::MaterialDomain::kMasked)
        desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
      for (unsigned c = 0; c < 3; ++c)
        desc.base_color[c] = color[c];
      desc.base_color[3]
        = domain == data::MaterialDomain::kAlphaBlended ? 0.5F : 1.0F;
      desc.roughness = data::Unorm16 { roughness };
      desc.metalness = data::Unorm16 { metallic };
      desc.ambient_occlusion = data::Unorm16 { 1.0F };
      desc.normal_scale = 1.0F;
      desc.uv_scale[0] = desc.uv_scale[1] = 1.0F;
      std::vector<content::ResourceKey> keys(6);
      keys[1] = normal;
      mesh_node.GetRenderable().SetMaterialOverride(0U, 0U,
        std::make_shared<data::MaterialAsset>(
          data::AssetKey::FromVirtualPath("/Test/IBL/Material-"
            + std::to_string(++material_sequence) + ".omat"),
          desc, std::vector<data::ShaderReference> {}, std::move(keys)));
    }

    std::array<std::uint32_t, 2> packed_input_slots {};
    std::array<std::shared_ptr<graphics::Texture>, 2> packed_input_textures;

    auto TearDown() -> void override
    {
      if (probe) {
        probe->after_render = {};
        probe->inspect = {};
      }
      packed_input_textures.fill(nullptr);
      ExposureLightingGpuTest::TearDown();
    }

    auto CapturePackedInputs(bool enabled) -> void
    {
      probe->after_render = {};
      packed_input_textures.fill(nullptr);
      if (!enabled)
        return;
      probe->after_render = [this](const ViewRenderGpuContext&) {
        auto* owner
          = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
            *renderer_);
        const auto& bindings = owner->GetSceneTextureBindings();
        auto& textures = owner->GetSceneTextures();
        // Capture while the view scope is active; SceneRenderer restores its
        // default resources when RenderSurface returns.
        packed_input_slots
          = { bindings.gbuffer_srvs[2], bindings.gbuffer_srvs[1] };
        packed_input_textures
          = { textures.GetGBufferBaseColor().shared_from_this(),
              textures.GetGBufferMaterial().shared_from_this() };
      };
    }

    auto PackedMaterialInputs() -> std::array<Pixel, 2>
    {
      // Reuse the typed texture-sampling probe. These are producer inputs,
      // not an invocation of the production IBL evaluator.
      const auto half = std::bit_cast<std::uint32_t>(0.5F);
      const auto one = std::bit_cast<std::uint32_t>(1.0F);
      constexpr std::uint32_t point_clamp_sampler = 2U;
      const auto inputs
        = std::array { std::array<std::uint32_t, 4> { packed_input_slots[0],
                         point_clamp_sampler, half, half },
            std::array<std::uint32_t, 4> { one, 0, 0, 0 },
            std::array<std::uint32_t, 4> {
              packed_input_slots[1], point_clamp_sampler, half, half },
            std::array<std::uint32_t, 4> { one, 0, 0, 0 } };
      const auto values = RunToneProbe(std::as_bytes(std::span(inputs)), 2U,
        512U, false, [&](graphics::CommandRecorder& recorder) {
          for (const auto& texture : packed_input_textures) {
            CHECK_F(recorder.AdoptKnownResourceState(*texture));
            recorder.RequireResourceState(
              *texture, graphics::ResourceStates::kShaderResource);
          }
        });
      return { Pixel { values[0][0], values[0][1], values[0][2], values[0][3] },
        Pixel { values[1][0], values[1][1], values[1][2], values[1][3] } };
    }

    auto Receiver(bool backface) -> void
    {
      const std::array positions { glm::vec3 { -2, -2, -1 },
        glm::vec3 { 2, -2, -1 }, glm::vec3 { 0, 2, -1 } };
      std::vector<data::Vertex> vertices(3);
      for (unsigned i = 0; i < 3; ++i) {
        vertices[i] = { .position = positions[i],
          .normal = { 0, 0, backface ? -1.0F : 1.0F },
          .texcoord = { 0.5F, 0.5F },
          .tangent = { 1, 0, 0 },
          .bitangent = { 0, backface ? -1.0F : 1.0F, 0 },
          .color = { 1, 1, 1, 1 } };
      }
      auto mesh
        = data::MeshBuilder()
            .WithVertices(vertices)
            .WithIndices(backface ? std::vector<std::uint32_t> { 0, 2, 1 }
                                  : std::vector<std::uint32_t> { 0, 1, 2 })
            .BeginSubMesh("IBL receiver", data::MaterialAsset::CreateDefault())
            .WithMeshView({ .first_index = 0,
              .index_count = 3,
              .first_vertex = 0,
              .vertex_count = 3 })
            .EndSubMesh()
            .Build();
      data::pak::geometry::GeometryAssetDesc desc {};
      desc.lod_count = 1;
      desc.bounding_box_min[0] = desc.bounding_box_min[1] = -2;
      desc.bounding_box_max[0] = desc.bounding_box_max[1] = 2;
      desc.bounding_box_min[2] = desc.bounding_box_max[2] = -1;
      mesh_node.GetRenderable().SetGeometry(
        std::make_shared<data::GeometryAsset>(
          data::AssetKey::FromVirtualPath("/Test/IBL/Receiver-"
            + std::to_string(++material_sequence) + ".ogeo"),
          desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) }));
    }
  };

  NOLINT_TEST_F(
    IblSurfaceGpuTest, SplitSumAndIndependentControlsAcrossNativeSurfacePaths)
  {
    const Pixel radiance { 0.25F, 0.5F, 1.0F, 1.0F };
    Sky().SetCubemapResource(
      Texture(16U, true, [&](auto, auto, auto) { return radiance; }));
    // Exact endpoints isolate IBL from sRGB GBuffer quantization. Zero green
    // also exercises the retained F90=50*F0.g rule on a colored metal.
    const std::array colors { glm::vec3 { 1.0F, 0.0F, 1.0F },
      glm::vec3 { 0.8F, 0.4F, 0.2F } };
    struct Controls {
      float diffuse;
      float specular;
      bool reflections;
      float intensity;
      glm::vec3 tint;
    };
    const std::array controls {
      Controls { 0, 0, true, 1, { 1, 1, 1 } },
      Controls { 1, 0, true, 1, { 1, 1, 1 } },
      Controls { 0, 1, true, 1, { 1, 1, 1 } },
      Controls { 1, 1, true, 1, { 1, 1, 1 } },
      Controls { 1, 1, false, 1, { 1, 1, 1 } },
      Controls { 0.25F, 1.75F, true, 2, { 0.5F, 1, 0.25F } },
    };
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended }) {
        const bool forward_shader
          = forward || domain == data::MaterialDomain::kAlphaBlended;
        CapturePackedInputs(!forward_shader);
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
        for (const auto albedo : colors) {
          for (const float metallic : { 0.0F, 1.0F }) {
            for (const float roughness : { 0.0F, 0.2F, 1.0F }) {
              Material(domain, roughness, metallic, albedo);
              auto material_color = albedo;
              auto material_specular = 0.5F;
              auto material_roughness = roughness;
              bool read_packed_inputs = !forward_shader;
              bool warm_material = true;
              for (const auto& control : controls) {
                Sky().SetDiffuseIntensity(control.diffuse);
                Sky().SetSpecularIntensity(control.specular);
                Sky().SetAffectReflections(control.reflections);
                Sky().SetIntensityMul(control.intensity);
                Sky().SetTintRgb(control.tint);
                for (const float ev : { 0.0F, 3.0F }) {
                  SCOPED_TRACE(::testing::Message()
                    << "case=" << cases << " forward=" << forward
                    << " domain=" << int(domain) << " metallic=" << metallic
                    << " roughness=" << roughness << " diffuse="
                    << control.diffuse << " specular=" << control.specular
                    << " reflections=" << control.reflections << " ev=" << ev);
                  // Only asset residency is warmed up. Gain/tint/lobe and
                  // exposure edits must reach the very next rendered frame.
                  const auto capture = cases == 6U
                    ? BeginOptionalCapture()
                    : observer_ptr<graphics::FrameCaptureController> {};
                  ASSERT_NO_FATAL_FAILURE(
                    RenderSurface(forward, ev, warm_material ? 5U : 1U));
                  warm_material = false;
                  if (capture)
                    EXPECT_TRUE(capture->EndCapture());
                  const auto pixel = ReadFloatTexture(*probe->color).at(0);
                  if (read_packed_inputs) {
                    const auto packed = PackedMaterialInputs();
                    for (unsigned c = 0; c < 3; ++c) {
                      EXPECT_NEAR(packed[0][c], albedo[c], 0.01F);
                      material_color[c] = packed[0][c];
                    }
                    EXPECT_FLOAT_EQ(packed[1][0], metallic);
                    EXPECT_NEAR(packed[1][1], 0.5F, 0.5F / 255.0F + 1.0e-6F);
                    EXPECT_NEAR(
                      packed[1][2], roughness, 0.5F / 255.0F + 1.0e-6F);
                    material_specular = packed[1][1];
                    material_roughness = packed[1][2];
                    read_packed_inputs = false;
                  }
                  const auto brdf = BrdfAtNormalIncidence(material_roughness);
                  const double f90 = std::min(1.0,
                    50.0
                      * (metallic > 0 ? material_color.g
                                      : 0.08 * material_specular));
                  for (unsigned c = 0; c < 3; ++c) {
                    const double f0 = metallic > 0 ? material_color[c]
                                                   : 0.08 * material_specular;
                    const double diffuse
                      = material_color[c] * (1 - metallic) * control.diffuse;
                    const double specular = control.reflections
                      ? (f0 * brdf[0] + f90 * brdf[1]) * control.specular
                      : 0.0;
                    const double expected = radiance[c] * control.intensity
                      * control.tint[c] * (diffuse + specular) * coverage
                      * std::exp2(-double(ev));
                    EXPECT_NEAR(
                      pixel[c], expected, std::abs(expected) * 0.001 + 1.0e-6);
                  }
                  EXPECT_FLOAT_EQ(pixel[3], float(coverage));
                  ++cases;
                }
              }
            }
          }
        }
      }
    }
    RecordProperty("ibl_raster_split_sum_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, NormalMapsAndBackfacesUseTheMaterialNormalForIrradiance)
  {
    Sky().SetCubemapResource(Texture(16U, true, [](auto face, auto x, auto y) {
      const auto z = WorldDirection(face, x, y, 16U).z;
      return Pixel { 0.75F + 0.5F * z, 0.5F + 0.25F * z, 0.25F + 0.125F * z,
        1.0F };
    }));
    Sky().SetSpecularIntensity(0.0F);
    const auto normal = Texture(1U, false,
      [](auto, auto, auto) { return Pixel { 0.8F, 0.5F, 0.9F, 1.0F }; });
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended }) {
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
        for (const bool backface : { false, true }) {
          Receiver(backface);
          for (const bool mapped : { false, true }) {
            Material(domain, 1.0F, 0.0F, glm::vec3(1.0F),
              mapped ? normal : content::ResourceKey {});
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << int(domain)
              << " backface=" << backface << " mapped=" << mapped);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto pixel = ReadFloatTexture(*probe->color).at(0);
            // Reversed winding has matching authored N/B. Double-sided
            // evaluation must orient the visible normal toward the camera.
            const double normal_z = mapped ? 0.8 : 1.0;
            const std::array base { 0.75, 0.5, 0.25 };
            const std::array slope { 0.5, 0.25, 0.125 };
            for (unsigned c = 0; c < 3; ++c) {
              const double expected
                = (base[c] + (2.0 / 3.0) * slope[c] * normal_z) * coverage;
              EXPECT_NEAR(
                pixel[c], expected, std::abs(expected) * 0.002 + 1.0e-6);
            }
            EXPECT_FLOAT_EQ(pixel[3], float(coverage));
            ++cases;
          }
        }
      }
    }
    RecordProperty("ibl_material_normal_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, GlossyAndRoughReflectionsRespondToNormalMapsAcrossPaths)
  {
    Sky().SetCubemapResource(Texture(32U, true, [](auto face, auto x, auto y) {
      const auto z = WorldDirection(face, x, y, 32U).z;
      return Pixel { 0.75F + 0.5F * z, 0.5F + 0.25F * z, 0.25F + 0.125F * z,
        1.0F };
    }));
    Sky().SetDiffuseIntensity(0.0F);
    const auto normal = Texture(1U, false,
      [](auto, auto, auto) { return Pixel { 0.8F, 0.5F, 0.9F, 1.0F }; });
    std::array<Pixel, 4> reference {};
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended }) {
        const float coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5F : 1.0F;
        std::array<Pixel, 4> actual {};
        unsigned index = 0;
        for (const float roughness : { 0.0F, 1.0F }) {
          for (const bool mapped : { false, true }) {
            Material(domain, roughness, 1.0F, glm::vec3(1.0F),
              mapped ? normal : content::ResourceKey {});
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << int(domain)
              << " roughness=" << roughness << " mapped=" << mapped);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            actual[index] = ReadFloatTexture(*probe->color).at(0);
            for (unsigned c = 0; c < 3; ++c)
              actual[index][c] /= coverage;
            if (!forward && domain == data::MaterialDomain::kOpaque)
              reference[index] = actual[index];
            else
              for (unsigned c = 0; c < 3; ++c) {
                // The deferred shading normal is octahedrally packed in
                // UNORM10.
                EXPECT_NEAR(actual[index][c], reference[index][c],
                  reference[index][c] * 0.01F + 1.0e-5F);
              }
            EXPECT_FLOAT_EQ(actual[index][3], coverage);
            ++index;
            ++cases;
          }
        }
        EXPECT_LT(actual[1][0], actual[0][0] * 0.95F);
        EXPECT_GT(actual[0][0], actual[2][0] * 2.0F);
      }
    }
    RecordProperty("ibl_raster_reflection_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, CapturedHeightFogLightsSurfacesWithoutMainViewFog)
  {
    Sky().SetSource(scene::environment::SkyLightSource::kCapturedScene);
    Sky().SetDiffuseIntensity(0.0F);
    auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
    fog.SetEnabled(true);
    fog.SetEnableHeightFog(true);
    fog.SetEnableVolumetricFog(false);
    fog.SetFogDensity(0.01F);
    fog.SetHeightFalloffPerMeter(0.0F);
    fog.SetMaxOpacity(1.0F);
    fog.SetFogInscatteringLuminance({ 128, 64, 32 });
    fog.SetRenderInMainPass(false);
    fog.SetVisibleInRealTimeSkyCaptures(true);
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : { data::MaterialDomain::kOpaque,
             data::MaterialDomain::kAlphaBlended }) {
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
        for (const float roughness : { 0.0F, 1.0F }) {
          Material(domain, roughness, 1.0F, glm::vec3(1.0F));
          const auto brdf = BrdfAtNormalIncidence(roughness);
          for (const float volumetric : { 0.0F, 4.0F }) {
            Sky().SetVolumetricScatteringIntensity(volumetric);
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << int(domain)
              << " roughness=" << roughness << " volumetric=" << volumetric);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto pixel = ReadFloatTexture(*probe->color).at(0);
            for (unsigned c = 0; c < 3; ++c) {
              const double expected
                = (128.0 / (1U << c)) * (brdf[0] + brdf[1]) * coverage;
              EXPECT_NEAR(pixel[c], expected, expected * 0.001 + 1.0e-5);
            }
            EXPECT_FLOAT_EQ(pixel[3], float(coverage));
            ++cases;
          }
        }
      }
    }
    Material(data::MaterialDomain::kOpaque, 0.0F, 1.0F, glm::vec3(1.0F));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    fog.SetFogInscatteringLuminance({ 64, 128, 32 });
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto edited = ReadFloatTexture(*probe->color).at(0);
    EXPECT_NEAR(edited[0], 64.0F, 0.002F);
    EXPECT_NEAR(edited[1], 128.0F, 0.004F);
    EXPECT_NEAR(edited[2], 32.0F, 0.001F);
    fog.SetVisibleInRealTimeSkyCaptures(false);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    auto dark = ReadFloatTexture(*probe->color).at(0);
    for (unsigned c = 0; c < 3; ++c)
      EXPECT_FLOAT_EQ(dark[c], 0.0F);
    fog.SetVisibleInRealTimeSkyCaptures(true);
    Sky().SetEnabled(false);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    dark = ReadFloatTexture(*probe->color).at(0);
    for (unsigned c = 0; c < 3; ++c)
      EXPECT_FLOAT_EQ(dark[c], 0.0F);
    RecordProperty("ibl_captured_surface_cases", cases + 3U);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, SpecularHdrScaleAndTinyChannelsSurviveRasterTransport)
  {
    const Pixel source { 0x1p20F, 0x1p-24F, 1.0F, 1.0F };
    Sky().SetCubemapResource(
      Texture(16U, true, [&](auto, auto, auto) { return source; }));
    Sky().SetDiffuseIntensity(0.0F);
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended }) {
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
        for (const float roughness : { 0.0F, 1.0F }) {
          Material(domain, roughness, 1.0F, glm::vec3(1.0F));
          const auto brdf = BrdfAtNormalIncidence(roughness);
          for (const float gain : { 1.0F, 1024.0F }) {
            Sky().SetIntensityMul(gain);
            for (const float ev : { -4.0F, 0.0F, 16.0F }) {
              SCOPED_TRACE(::testing::Message()
                << "forward=" << forward << " domain=" << int(domain)
                << " roughness=" << roughness << " gain=" << gain
                << " ev=" << ev);
              ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
              const auto pixel = ReadFloatTexture(*probe->color).at(0);
              for (unsigned c = 0; c < 3; ++c) {
                const double expected = double(source[c]) * gain
                  * (brdf[0] + brdf[1]) * coverage * std::exp2(-double(ev));
                EXPECT_NEAR(pixel[c], expected, expected * 2.0e-5 + 0x1p-120);
              }
              EXPECT_FLOAT_EQ(pixel[3], float(coverage));
              ++cases;
            }
          }
        }
      }
    }
    RecordProperty("ibl_specular_hdr_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, CapturedAtmosphereAndFogHalfMatchesCanonicalAcrossPaths)
  {
    Sky().SetSource(scene::environment::SkyLightSource::kCapturedScene);
    Sky().SetDiffuseIntensity(0.0F);
    scene->GetEnvironment()
      ->AddSystem<scene::environment::SkyAtmosphere>()
      .SetEnabled(true);
    auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
    fog.SetEnabled(true);
    fog.SetEnableHeightFog(true);
    fog.SetEnableVolumetricFog(false);
    fog.SetFogDensity(0.001F);
    fog.SetHeightFalloffPerMeter(0.001F);
    fog.SetMaxOpacity(0.75F);
    fog.SetFogInscatteringLuminance({ 0.23F, 0.41F, 0.61F });
    fog.SetRenderInMainPass(false);
    fog.SetVisibleInRealTimeSkyCaptures(true);
    auto sun = scene->CreateNode("IBL capture sun");
    auto light = std::make_unique<scene::DirectionalLight>();
    light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    light->SetIntensityLux(1000.0F);
    ASSERT_TRUE(sun.AttachLight(std::move(light)));
    renderer_->GetDiagnosticsService().SetHdrFp32ReferenceEnabled(true);
    auto maximum_rgb_error = 0.0;
    auto maximum_ev_error = 0.0;
    unsigned changed_channels = 0U;
    unsigned cases = 0U;
    std::shared_ptr<const environment::internal::IblGpuProducts> published;
    probe->inspect = [&](const auto& context, const auto&, unsigned) {
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      published
        = vortex::testing::RendererPublicationProbe::PublishedIblProducts(
          *owner, context.current_view.view_id);
    };
    for (const bool forward : { false, true })
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended })
        for (const float roughness : { 0.0F, 0.1F, 0.5F, 1.0F })
          for (const auto tint :
            { glm::vec3 { 1, 1, 1 }, glm::vec3 { 0, 1, 0 } }) {
            SCOPED_TRACE(::testing::Message() << "case=" << cases);
            Material(domain, roughness, 1.0F, glm::vec3(1.0F));
            Sky().SetTintRgb(tint);
            Sky().SetSpecularIntensity(0.0F);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto baseline = ReadFloatTexture(*probe->color).at(0);
            const auto products = published;
            ASSERT_NE(products, nullptr);
            const auto metadata = Read<environment::IblProductMetadata>(
              *products->metadata, graphics::ResourceStates::kShaderResource);
            ASSERT_EQ(metadata.precision_flags,
              environment::kIblHalfCertificateComplete);
            ASSERT_GT(metadata.maximum_half_gain, 1.0F);
            Sky().SetSpecularIntensity(1.0F);
            auto canonical_metadata = metadata;
            canonical_metadata.precision_flags = 0U;
            const auto capture = cases == 0U
              ? BeginOptionalCapture()
              : observer_ptr<graphics::FrameCaptureController> {};
            ASSERT_NO_FATAL_FAILURE(
              WriteComparisonMetadata(*products, canonical_metadata));
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
            const auto reference = ReadFloatTexture(*probe->color).at(0);
            ASSERT_NO_FATAL_FAILURE(
              WriteComparisonMetadata(*products, metadata));
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
            const auto actual = ReadFloatTexture(*probe->color).at(0);
            if (capture)
              EXPECT_TRUE(capture->EndCapture());
            ASSERT_NE(published, nullptr);
            EXPECT_EQ(published->revision, products->revision);
            EXPECT_EQ(published.get(), products.get());
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double a = double(actual[channel]) - baseline[channel];
              const double r = double(reference[channel]) - baseline[channel];
              if (tint[channel] == 0.0F) {
                EXPECT_NEAR(a, 0.0, 1.0e-5);
                EXPECT_NEAR(r, 0.0, 1.0e-5);
                continue;
              }
              ASSERT_GT(a, 0.0);
              ASSERT_GT(r, 0.0);
              maximum_rgb_error
                = std::max(maximum_rgb_error, std::abs(a - r) / r);
              maximum_ev_error
                = std::max(maximum_ev_error, std::abs(std::log2(a / r)));
              if (a != r)
                ++changed_channels;
            }
            EXPECT_EQ(actual[3], reference[3]);
            ++cases;
          }
    EXPECT_GT(changed_channels, 0U);
    probe->inspect = {};
    EXPECT_LE(maximum_rgb_error, 0.0025);
    EXPECT_LE(maximum_ev_error, 2.0 / 1024.0);
    RecordProperty("captured_half_surface_cases", cases);
    RecordProperty("captured_half_surface_max_relative_rgb",
      std::to_string(maximum_rgb_error));
    RecordProperty(
      "captured_half_surface_max_ev", std::to_string(maximum_ev_error));
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, GainOnlyEditsReachNextFrameWithoutRecapture)
  {
    const Pixel radiance { 0.25F, 0.5F, 1.0F, 1.0F };
    Sky().SetCubemapResource(
      Texture(16U, true, [&](auto, auto, auto) { return radiance; }));
    std::uint32_t revision = 0;
    probe->inspect = [&](const auto&, const auto&, unsigned) {
      revision = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_)
                   ->GetLastEnvironmentLightingState()
                   .probe_revision;
    };
    struct Controls {
      float intensity;
      float diffuse;
      float specular;
      bool reflections;
      glm::vec3 tint;
    };
    const std::array controls {
      Controls { 1, 1, 1, true, { 1, 1, 1 } },
      Controls { 0, 1, 1, true, { 1, 1, 1 } },
      Controls { 3, 1, 1, true, { 1, 1, 1 } },
      Controls { 3, 0, 1, true, { 1, 1, 1 } },
      Controls { 3, 0.25F, 1, true, { 1, 1, 1 } },
      Controls { 3, 0.25F, 1, true, { 0.5F, 0.25F, 0.125F } },
      Controls { 3, 0.25F, 0, true, { 0.5F, 0.25F, 0.125F } },
      Controls { 3, 0.25F, 1, false, { 0.5F, 0.25F, 0.125F } },
    };
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : { data::MaterialDomain::kOpaque,
             data::MaterialDomain::kAlphaBlended }) {
        const bool forward_shader
          = forward || domain == data::MaterialDomain::kAlphaBlended;
        CapturePackedInputs(!forward_shader);
        Material(domain, 1.0F, 0.0F, glm::vec3(1.0F));
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
        const auto expected_revision = revision;
        ASSERT_GT(expected_revision, 0U);
        const double f0
          = forward_shader ? 0.04 : 0.08 * PackedMaterialInputs()[1][1];
        const auto brdf = BrdfAtNormalIncidence(1.0F);
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
        for (const auto& c : controls) {
          Sky().SetIntensityMul(c.intensity);
          Sky().SetDiffuseIntensity(c.diffuse);
          Sky().SetSpecularIntensity(c.specular);
          Sky().SetAffectReflections(c.reflections);
          Sky().SetTintRgb(c.tint);
          SCOPED_TRACE(::testing::Message()
            << "case=" << cases << " forward=" << forward
            << " domain=" << int(domain));
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
          EXPECT_EQ(revision, expected_revision);
          const auto pixel = ReadFloatTexture(*probe->color).at(0);
          for (unsigned channel = 0; channel < 3; ++channel) {
            const double expected = radiance[channel] * c.intensity
              * c.tint[channel]
              * (c.diffuse
                + (c.reflections ? c.specular * (f0 * brdf[0] + brdf[1]) : 0))
              * coverage;
            EXPECT_NEAR(pixel[channel], expected, expected * 0.001 + 1.0e-6);
          }
          ++cases;
        }
      }
    }
    probe->inspect = {};
    RecordProperty("ibl_immediate_gain_cases", cases);
  }

} // namespace
} // namespace oxygen::vortex::testing::exposure
