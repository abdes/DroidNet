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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ScopeGuard.h>
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
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/Test/CommandRecordingTestSupport.h>
#include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
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

    auto SetCapturedUniformFog() -> scene::environment::Fog&
    {
      Sky().SetSource(scene::environment::SkyLightSource::kCapturedScene);
      Sky().SetSpecularIntensity(0.0F);
      auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
      fog.SetEnabled(true);
      fog.SetEnableHeightFog(true);
      fog.SetFogDensity(0.01F);
      fog.SetHeightFalloffPerMeter(0.0F);
      fog.SetMaxOpacity(1.0F);
      fog.SetFogInscatteringLuminance({ 1, 1, 1 });
      fog.SetRenderInMainPass(false);
      fog.SetVisibleInRealTimeSkyCaptures(true);
      Material(data::MaterialDomain::kOpaque, 1.0F, 0.0F, glm::vec3(1.0F));
      return fog;
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

    std::array<std::uint32_t, 3> packed_input_slots {};
    std::array<std::shared_ptr<graphics::Texture>, 3> packed_input_textures;

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
        packed_input_slots = { bindings.gbuffer_srvs[2],
          bindings.gbuffer_srvs[1], bindings.gbuffer_srvs[0] };
        packed_input_textures
          = { textures.GetGBufferBaseColor().shared_from_this(),
              textures.GetGBufferMaterial().shared_from_this(),
              textures.GetGBufferNormal().shared_from_this() };
      };
    }

    auto PackedSurfaceInputs() -> std::array<Pixel, 3>
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
            std::array<std::uint32_t, 4> { one, 0, 0, 0 },
            std::array<std::uint32_t, 4> {
              packed_input_slots[2], point_clamp_sampler, half, half },
            std::array<std::uint32_t, 4> { one, 0, 0, 0 } };
      const auto values = RunToneProbe(std::as_bytes(std::span(inputs)), 3U,
        512U, false, [&](graphics::CommandRecorder& recorder) {
          for (const auto& texture : packed_input_textures) {
            CHECK_F(recorder.AdoptKnownResourceState(*texture));
            recorder.RequireResourceState(
              *texture, graphics::ResourceStates::kShaderResource);
          }
        });
      return { Pixel { values[0][0], values[0][1], values[0][2], values[0][3] },
        Pixel { values[1][0], values[1][1], values[1][2], values[1][3] },
        Pixel { values[2][0], values[2][1], values[2][2], values[2][3] } };
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

  class IblDiagnosticsGpuTest : public IblSurfaceGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling;
    }

    auto IblOwner() -> environment::internal::IblProcessor&
    {
      return RendererPublicationProbe::IblOwner(
        *RendererPublicationProbe::GetSceneRenderer(*renderer_));
    }
  };

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, MetadataCollectionFollowsFrameDiagnostics)
  {
    auto& fog = SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kNotRequested);
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kPending);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto state = renderer_->InspectSkyLight(*scene);
    EXPECT_EQ(state.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(state.validated_revision, state.published_revision);
    EXPECT_FLOAT_EQ(state.source_radiance_scale, 1.0F);
    EXPECT_NEAR(state.average_brightness, 1.0F, 0.0001F);
    EXPECT_EQ(state.last_failed_gpu_revision, 0U);
    EXPECT_FALSE(diagnostics.IsGpuTimelineEnabled());
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    fog.SetFogInscatteringLuminance(glm::vec3(4.0F));
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kNotRequested);
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 4.0F, 0.001F);
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 2U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kValid);
    EXPECT_NEAR(
      renderer_->InspectSkyLight(*scene).average_brightness, 4.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, GpuInvalidGenerationIsZeroAndRemainsVisible)
  {
    SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto products = IblOwner().GetPublishedProducts();
    ASSERT_TRUE(products);
    auto metadata = Read<environment::IblProductMetadata>(
      *products->metadata, graphics::ResourceStates::kShaderResource);
    metadata.processing_flags = environment::kIblProductComplete;
    WriteComparisonMetadata(*products, metadata);
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kPending);
    EXPECT_EQ(ReadFloatTexture(*probe->color).front()[0], 0.0F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto state = renderer_->InspectSkyLight(*scene);
    EXPECT_TRUE(state.usable); // CPU submission was accepted; GPU rejected it.
    EXPECT_EQ(state.gpu_validation, SkyLightGpuValidation::kInvalid);
    EXPECT_EQ(state.validated_revision, products->revision);
    EXPECT_EQ(state.last_failed_gpu_revision, products->revision);
    const auto ledger = diagnostics.GetLatestSnapshot();
    EXPECT_TRUE(std::ranges::any_of(ledger.issues, [](const auto& issue) {
      return issue.code == "ibl.gpu-invalid"
        && issue.severity == DiagnosticsSeverity::kError;
    }));
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 2U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kInvalid);
    EXPECT_EQ(ReadFloatTexture(*probe->color).front()[0], 0.0F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, LateInvalidObservationDoesNotRejectNewGeneration)
  {
    auto& fog = SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto products = IblOwner().GetPublishedProducts();
    ASSERT_TRUE(products);
    auto metadata = Read<environment::IblProductMetadata>(
      *products->metadata, graphics::ResourceStates::kShaderResource);
    metadata.product_revision = 0U;
    WriteComparisonMetadata(*products, metadata);
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    fog.SetFogInscatteringLuminance(glm::vec3(8.0F));
    scene->NotifyEnvironmentAuthoringChange();
    // Frame start requests the old generation; authoring publishes its
    // successor.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    ASSERT_NE(renderer_->InspectSkyLight(*scene).published_revision,
      products->revision);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto pending = renderer_->InspectSkyLight(*scene);
    EXPECT_EQ(pending.last_failed_gpu_revision, products->revision);
    EXPECT_EQ(pending.gpu_validation, SkyLightGpuValidation::kPending);
    EXPECT_EQ(pending.validated_revision, 0U);
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 8.0F, 0.001F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto valid = renderer_->InspectSkyLight(*scene);
    EXPECT_EQ(valid.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(valid.validated_revision, valid.published_revision);
    EXPECT_EQ(valid.last_failed_gpu_revision, products->revision);
    EXPECT_NEAR(valid.average_brightness, 8.0F, 0.001F);
  }

#if defined(_MSC_VER) && defined(_DEBUG)
  NOLINT_TEST_F(IblDiagnosticsGpuTest,
    DiagnosticAllocationFailurePreservesLightingAndRetries)
  {
    SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto products = IblOwner().GetPublishedProducts();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    {
      graphics::testing::HeapAllocationFailure denied;
      (void)IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U });
    }
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kUnavailable);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kValid);
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 1.0F, 0.001F);
  }
#endif

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, RejectedMetadataCopyPreservesLightingAndRetries)
  {
    SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto products = IblOwner().GetPublishedProducts();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    graphics::internal::SubmissionFaultTestAccess::FailNext(
      *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
    EXPECT_FALSE(
      IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U }));
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kUnavailable);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kValid);
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 1.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, UncertainMetadataCopyInvalidatesPublication)
  {
    SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    graphics::internal::SubmissionFaultTestAccess::FailNext(*GetQueue(),
      graphics::internal::SubmissionFailurePoint::kAfterIssueBeforeMarker);
    const auto backend = Backend().GetBackendLifetime();
    const ScopeGuard restore(
      [backend]() noexcept { backend->ClearSubmissionFault(); });
    EXPECT_TRUE(
      IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U }));
    EXPECT_EQ(IblOwner().GetPublishedProducts(), nullptr);
    EXPECT_FALSE(renderer_->InspectSkyLight(*scene).usable);
    backend->ClearSubmissionFault();
    EXPECT_EQ(IblOwner().GetPublishedProducts(), nullptr);
    WaitForQueueIdle();
  }

  NOLINT_TEST_F(IblDiagnosticsGpuTest, ReportsOnlyCompleteRequestedGpuTimings)
  {
    auto& fog = SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    EXPECT_FALSE(diagnostics.GetLatestIblGpuTiming());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    for (unsigned edit = 0U; edit < 3U; ++edit) {
      fog.SetFogInscatteringLuminance(glm::vec3(float(edit + 10U)));
      scene->NotifyEnvironmentAuthoringChange();
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    }
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
    diagnostics.SetGpuTimelineEnabled(true);
    // Earlier budget-only samples did not collect the complete producer path.
    EXPECT_FALSE(diagnostics.GetLatestIblGpuTiming());
    diagnostics.SetGpuTimelineMaxScopesPerFrame(1024U);
    for (unsigned edit = 0U; edit < 8U; ++edit) {
      fog.SetFogInscatteringLuminance(glm::vec3(float(edit + 1U)));
      scene->NotifyEnvironmentAuthoringChange();
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    }
    const auto timing = diagnostics.GetLatestIblGpuTiming();
    ASSERT_TRUE(timing);
    EXPECT_GT(timing->producer_ms, 0.0);
    const auto measured_frame
      = RendererPublicationProbe::GetGpuTimelineProfiler(*renderer_)
          .GetLastPublishedFrame();
    ASSERT_TRUE(measured_frame);
    EXPECT_EQ(timing->frame_sequence, measured_frame->frame_sequence);
    const auto process
      = std::ranges::find_if(measured_frame->scopes, [](const auto& scope) {
          return scope.display_name == "Vortex.Environment.IBL.Process";
        });
    ASSERT_NE(process, measured_frame->scopes.end());
    // Fog-only authoring has one Process interval and no atmosphere LUT work.
    EXPECT_NEAR(timing->producer_ms, process->duration_ms, 0.00001);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 5U));
    ASSERT_TRUE(diagnostics.GetLatestIblGpuTiming());
    EXPECT_EQ(diagnostics.GetLatestIblGpuTiming()->producer_ms, 0.0);
    diagnostics.SetGpuTimelineEnabled(false);
    EXPECT_FALSE(diagnostics.GetLatestIblGpuTiming());
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, MetadataReadbackStorageStabilizesAcrossAuthoring)
  {
    auto& fog = SetCapturedUniformFog();
    renderer_->GetDiagnosticsService().SetEnabledFeatures(
      DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    std::size_t registrations = 0U;
    for (unsigned edit = 0U; edit < 24U; ++edit) {
      fog.SetFogInscatteringLuminance(glm::vec3(float(edit + 2U)));
      scene->NotifyEnvironmentAuthoringChange();
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
      Backend().PollCompletedUses();
      const auto current
        = Backend().GetResourceRegistry().GetRegisteredResourceCount();
      if (edit == 7U)
        registrations = current;
      if (edit > 7U)
        EXPECT_EQ(current, registrations);
      EXPECT_EQ(
        renderer_->InspectSkyLight(*scene).last_failed_gpu_revision, 0U);
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).gpu_validation,
      SkyLightGpuValidation::kValid);
    EXPECT_NEAR(
      renderer_->InspectSkyLight(*scene).average_brightness, 25.0F, 0.001F);
    RecordProperty("authoring_generations", 24U);
    RecordProperty("steady_registered_resources", registrations);
  }

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
                    const auto packed = PackedSurfaceInputs();
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
    scene->NotifyEnvironmentAuthoringChange();
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

  NOLINT_TEST_F(
    IblSurfaceGpuTest, RepeatedAuthoringBatchesPublishTheLatestFogSnapshot)
  {
    Sky().SetSource(scene::environment::SkyLightSource::kCapturedScene);
    Sky().SetSpecularIntensity(0.0F);
    auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
    fog.SetEnabled(true);
    fog.SetEnableHeightFog(true);
    fog.SetFogDensity(0.01F);
    fog.SetHeightFalloffPerMeter(0.0F);
    fog.SetMaxOpacity(1.0F);
    fog.SetFogInscatteringLuminance({ 1, 2, 3 });
    fog.SetRenderInMainPass(false);
    fog.SetVisibleInRealTimeSkyCaptures(true);
    Material(data::MaterialDomain::kOpaque, 1.0F, 0.0F, glm::vec3(1.0F));
    std::uint32_t revision = 0U;
    probe->inspect = [&](const auto& context, const auto&, unsigned) {
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      const auto products
        = vortex::testing::RendererPublicationProbe::PublishedIblProducts(
          *owner, context.current_view.view_id);
      ASSERT_TRUE(products);
      revision = products->revision;
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    for (unsigned edit = 1U; edit <= 12U; ++edit) {
      SCOPED_TRACE(edit);
      const auto previous = revision;
      const glm::vec3 wanted { float(edit), 0.5F * edit, 0.25F * edit };
      fog.SetFogInscatteringLuminance(wanted * 3.0F);
      scene->NotifyEnvironmentAuthoringChange();
      fog.SetFogInscatteringLuminance(wanted);
      scene->NotifyEnvironmentAuthoringChange();
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
      const auto actual = ReadFloatTexture(*probe->color).front();
      for (unsigned c = 0U; c < 3U; ++c)
        EXPECT_NEAR(actual[c], wanted[c], wanted[c] * 0.0001F);
      EXPECT_EQ(revision, previous + 1U);
      EXPECT_EQ(scene->GetEnvironmentAuthoringRevision(), 2U * edit);
    }
    RecordProperty("immediate_authoring_batches", 12U);
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, RuntimeFogUpdatesPublishWithoutStarvation)
  {
    auto& fog = SetCapturedUniformFog();
    const auto first_snapshot_frame = sequence + 1U;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto warmup_end = sequence;
    const auto capture = BeginOptionalCapture();
    float last_published = 1.0F;
    unsigned publications = 0U;
    unsigned longest_wait = 0U;
    unsigned wait = 0U;
    for (unsigned edit = 1U; edit <= 16U; ++edit) {
      SCOPED_TRACE(edit);
      const auto wanted = float(edit + 1U);
      fog.SetFogInscatteringLuminance(glm::vec3(wanted));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
      const auto actual = ReadFloatTexture(*probe->color).front()[0];
      ++wait;
      if (std::abs(actual - last_published) > 0.01F) {
        EXPECT_GT(actual, last_published);
        last_published = actual;
        ++publications;
        longest_wait = std::max(longest_wait, wait);
        wait = 0U;
      }
      EXPECT_LE(wait, 3U);
      EXPECT_LE(wanted - actual, 8.001F);
      EXPECT_LE(actual, wanted + 0.001F);
      const auto status = renderer_->InspectSkyLight(*scene);
      EXPECT_TRUE(status.observed);
      EXPECT_TRUE(status.usable);
      EXPECT_EQ(status.scene_lifetime, scene->GetLifetimeId().get());
      EXPECT_EQ(status.frame_sequence, sequence);
      const auto snapshot_frame = last_published < 1.5F
        ? first_snapshot_frame
        : warmup_end + static_cast<unsigned>(std::lround(last_published)) - 1U;
      EXPECT_EQ(status.source_age_frames, sequence - snapshot_frame);
      if (edit == 4U && capture)
        EXPECT_TRUE(capture->EndCapture());
    }
    EXPECT_GE(publications, 4U);
    EXPECT_LE(longest_wait, 4U);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 4U));
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 17.0F, 0.002F);
    const auto current = renderer_->InspectSkyLight(*scene);
    EXPECT_EQ(current.source_age_frames, 0U);
    EXPECT_EQ(current.building_revision, 0U);
    const auto other = std::make_shared<scene::Scene>("Unrendered scene", 8U);
    EXPECT_FALSE(renderer_->InspectSkyLight(*other).observed);
    RecordProperty("runtime_frames", 20U);
    RecordProperty("runtime_publications", publications);
    RecordProperty("maximum_publication_gap", longest_wait);
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, AuthoringPreemptsRuntimeCandidate)
  {
    auto& fog = SetCapturedUniformFog();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    fog.SetFogInscatteringLuminance(glm::vec3(2.0F));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 2U));
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 1.0F, 0.001F);
    fog.SetFogInscatteringLuminance(glm::vec3(8.0F));
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 8.0F, 0.001F);
    for (unsigned frame_index = 0U; frame_index < 6U; ++frame_index) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
      EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 8.0F, 0.001F);
    }
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, UnchangedKeyConsumesAuthoringIntent)
  {
    auto& fog = SetCapturedUniformFog();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    fog.SetFogInscatteringLuminance(glm::vec3(4.0F));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 1.0F, 0.001F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 4.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, WorkBudgetLearnsWithDiagnosticsDisabledThenIdles)
  {
    auto& fog = SetCapturedUniformFog();
    auto& diagnostics = renderer_->GetDiagnosticsService();
    diagnostics.SetEnabledFeatures(DiagnosticsFeature::kNone);
    diagnostics.SetGpuTimelineEnabled(false);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    auto* scene_renderer
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto& owner
      = vortex::testing::RendererPublicationProbe::IblOwner(*scene_renderer);
    const auto before = owner.GetTimingSampleCount();
    for (unsigned edit = 0U; edit < 16U; ++edit) {
      fog.SetFogInscatteringLuminance(glm::vec3(float(edit + 2U)));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
      EXPECT_FALSE(diagnostics.IsGpuTimelineEnabled());
    }
    ASSERT_GT(owner.GetTimingSampleCount(), before);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 12U));
    const auto settled = owner.GetTimingSampleCount();
    auto& profiler
      = vortex::testing::RendererPublicationProbe::GetGpuTimelineProfiler(
        *renderer_);
    const auto final_timing = profiler.GetLastPublishedFrame();
    ASSERT_TRUE(final_timing);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 6U));
    EXPECT_EQ(owner.GetTimingSampleCount(), settled);
    ASSERT_TRUE(profiler.GetLastPublishedFrame());
    EXPECT_EQ(profiler.GetLastPublishedFrame()->frame_sequence,
      final_timing->frame_sequence);
    EXPECT_FALSE(diagnostics.IsGpuTimelineEnabled());
    RecordProperty("ibl_timing_samples", settled);
  }

#if defined(_MSC_VER) && defined(_DEBUG)
  NOLINT_TEST_F(IblSurfaceGpuTest, SourceResolutionAllocationHonorsUpdatePolicy)
  {
    const auto cube = Texture(
      16U, true, [](auto, auto, auto) { return Pixel { 2, 2, 2, 1 }; });
    Sky().SetCubemapResource(cube);
    Material(data::MaterialDomain::kOpaque, 1.0F, 0.0F, glm::vec3(1.0F));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    auto* scene_renderer
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto binder
      = vortex::testing::RendererPublicationProbe::SkyTextureBinder(
        *scene_renderer);
    ASSERT_TRUE(binder);
    {
      auto resident = binder->AcquireReadyTexture(cube);
      ASSERT_TRUE(resident);
      const auto weak = std::weak_ptr(resident);
      resident.reset();
      ASSERT_TRUE(weak.expired());
    }
    namespace env = environment::internal;
    using graphics::testing::HeapAllocationFailure;
    for (const auto [replacement, authoring] :
      { std::pair { false, false }, std::pair { false, true },
        std::pair { true, false }, std::pair { true, true } }) {
      SCOPED_TRACE(::testing::Message()
        << "replacement=" << replacement << " authoring=" << authoring);
      auto processor = env::IblProcessor(*renderer_);
      auto state = env::StableAtmosphereState {};
      state.view_products.sky_light.enabled = true;
      state.view_products.sky_light.lower_hemisphere_is_solid_color = false;
      if (!replacement) {
        state.view_products.sky_light.source
          = environment::kSkyLightSourceSpecifiedCubemap;
        state.view_products.sky_light.cubemap_resource = cube;
      }
      auto context = RenderContext {};
      context.scene = observer_ptr { scene.get() };
      context.frame_sequence = frame::SequenceNumber { 1U };
      context.frame_slot = frame::Slot { 0U };
      ASSERT_TRUE(
        processor.RefreshSkyLightProducts({}, context, state, {}, binder)
          .refreshed);
      WaitForQueueIdle();
      const auto original = processor.GetPublishedProducts();
      unsigned failure_frame = 2U;
      if (!replacement && !authoring) {
        context.frame_sequence = frame::SequenceNumber { failure_frame };
        context.frame_slot = frame::Slot { 1U };
        env::IblProcessor::RefreshState same_source_failure;
        {
          HeapAllocationFailure denied;
          same_source_failure
            = processor.RefreshSkyLightProducts({}, context, state, {}, binder);
        }
        EXPECT_TRUE(same_source_failure.probe_state.valid);
        const auto snapshot
          = processor.InspectState(scene->GetLifetimeId().get());
        EXPECT_TRUE(snapshot.usable);
        EXPECT_EQ(snapshot.face_size, 16U);
        EXPECT_EQ(snapshot.source_age_frames, 0U);
        EXPECT_EQ(
          snapshot.desired_source_revision, snapshot.published_source_revision);
        ++failure_frame;
      }
      state.view_products.sky_light.source
        = environment::kSkyLightSourceSpecifiedCubemap;
      state.view_products.sky_light.cubemap_resource = cube;
      state.view_products.sky_light.source_cubemap_angle_radians = 0.25F;
      state.authoring_revision = authoring ? 1U : 0U;
      context.frame_sequence = frame::SequenceNumber { failure_frame };
      context.frame_slot = frame::Slot { (failure_frame - 1U) % 3U };
      env::IblProcessor::RefreshState rejected;
      const auto failures = HeapAllocationFailure::RejectedCount();
      {
        // GetOrAllocate hits the resident cache; AcquireReadyTexture must
        // allocate its lease before update policy is fully classified.
        HeapAllocationFailure denied;
        rejected
          = processor.RefreshSkyLightProducts({}, context, state, {}, binder);
      }
      EXPECT_GT(HeapAllocationFailure::RejectedCount(), failures);
      const bool retain_prior = !replacement && !authoring;
      EXPECT_EQ(rejected.probe_state.valid, retain_prior);
      EXPECT_EQ(rejected.probe_state.static_sky_light.unavailable_reason,
        environment::StaticSkyLightUnavailableReason::kProcessingFailed);
      EXPECT_EQ(
        processor.GetPublishedProducts(), retain_prior ? original : nullptr);
      bool refreshed = false;
      for (unsigned frame_number = failure_frame + 1U;
        frame_number <= failure_frame + (retain_prior ? 4U : 1U);
        ++frame_number) {
        context.frame_sequence = frame::SequenceNumber { frame_number };
        context.frame_slot = frame::Slot { (frame_number - 1U) % 3U };
        const auto retry
          = processor.RefreshSkyLightProducts({}, context, state, {}, binder);
        EXPECT_TRUE(retry.probe_state.valid);
        refreshed = refreshed || retry.refreshed;
      }
      EXPECT_TRUE(refreshed);
      WaitForQueueIdle();
    }
  }
#endif

  NOLINT_TEST_F(
    IblSurfaceGpuTest, ViewRecreationPreservesSharedIblAndRetainedCapture)
  {
    Sky().SetCubemapResource(Texture(
      16U, true, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; }));
    Sky().SetSpecularIntensity(0.0F);
    Material(data::MaterialDomain::kOpaque, 1.0F, 0.0F, glm::vec3(1.0F));
    auto acquired
      = Result<environment::IblCaptureLease, environment::IblCaptureError>(
        Err(environment::IblCaptureError::kUnavailable));
    auto published_id = kInvalidViewId;
    probe->inspect = [&](const auto& context, const auto&, unsigned) {
      published_id = context.current_view.view_id;
      acquired = renderer_->AcquireIblCapture(context.current_view.view_id);
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    probe->draws = 0U;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(false));
    ASSERT_TRUE(acquired);
    const auto retained = *acquired;
    const auto id = published_id;
    ASSERT_TRUE(renderer_->AcquireIblCapture(id));
    renderer_->RemovePublishedRuntimeView(frame, ViewId { surface_view_id });
    const auto removed = renderer_->AcquireIblCapture(id);
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error(), environment::IblCaptureError::kUnavailable);
    acquired = Err(environment::IblCaptureError::kUnavailable);
    probe->draws = 0U;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(false));
    ASSERT_TRUE(acquired);
    ASSERT_TRUE(renderer_->AcquireIblCapture(published_id));
    EXPECT_EQ(acquired->Revision(), retained.Revision());
    EXPECT_EQ(acquired->ProcessedCube(), retained.ProcessedCube());
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 2.0F, 0.0002F);
    auto readback = GetReadbackManager()->CreateBufferReadback(
      "IBL retained through view recreation");
    SubmitCommands(
      "IBL retained view metadata", [&](graphics::CommandRecorder& recorder) {
        ASSERT_TRUE(retained.Attach(recorder, Backend().GetResourceRegistry()));
        recorder.FlushBarriers();
        ASSERT_TRUE(readback->EnqueueCopy(recorder, *retained.Metadata(),
          { 0U, sizeof(environment::IblProductMetadata) }));
      });
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped);
    environment::IblProductMetadata metadata;
    std::memcpy(&metadata, mapped->Bytes().data(), sizeof(metadata));
    EXPECT_EQ(metadata.product_revision, retained.Revision());
    EXPECT_EQ(metadata.processing_flags, 3U);
    RecordProperty("recreated_view_retained_revision", retained.Revision());
  }

  NOLINT_TEST_F(IblSurfaceGpuTest,
    RendererCaptureAdmissionPreservesLiveUpdatesAndRetainedReads)
  {
    const auto unavailable
      = renderer_->AcquireIblCapture(ViewId { surface_view_id });
    ASSERT_FALSE(unavailable);
    EXPECT_EQ(unavailable.error(), environment::IblCaptureError::kUnavailable);
    Sky().SetSpecularIntensity(0.0F);
    Material(data::MaterialDomain::kOpaque, 1.0F, 0.0F, glm::vec3(1.0F));
    auto acquired
      = Result<environment::IblCaptureLease, environment::IblCaptureError>(
        Err(environment::IblCaptureError::kUnavailable));
    probe->inspect = [&](const auto& context, const auto&, unsigned) {
      acquired = renderer_->AcquireIblCapture(context.current_view.view_id);
    };
    const auto publish = [&](float value) {
      Sky().SetCubemapResource(Texture(16U, true, [value](auto, auto, auto) {
        return Pixel { value, value, value, 1 };
      }));
      RenderSurface(false, 0.0F);
    };
    ASSERT_NO_FATAL_FAILURE(publish(1.0F));
    ASSERT_TRUE(acquired);
    auto first = std::move(*acquired);
    ASSERT_NO_FATAL_FAILURE(publish(2.0F));
    ASSERT_TRUE(acquired);
    auto second = std::move(*acquired);
    EXPECT_GT(second.Revision(), first.Revision());
    ASSERT_NO_FATAL_FAILURE(publish(3.0F));
    ASSERT_FALSE(acquired);
    EXPECT_EQ(acquired.error(), environment::IblCaptureError::kBusy);
    EXPECT_NEAR(ReadFloatTexture(*probe->color).front()[0], 3.0F, 0.003F);
    first = {};
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    ASSERT_TRUE(acquired);
    auto third = std::move(*acquired);
    EXPECT_GT(third.Revision(), second.Revision());
    probe->inspect = {};
    const auto capture = BeginOptionalCapture();
    renderer_->OnShutdown();
    EXPECT_FALSE(renderer_->AcquireIblCapture(ViewId { surface_view_id }));
    auto readback = GetReadbackManager()->CreateTextureReadback(
      "Retained renderer IBL capture");
    auto metadata_readback = GetReadbackManager()->CreateBufferReadback(
      "Retained renderer IBL metadata");
    graphics::testing::SubmitCommands(Backend(), "IBL capture after shutdown",
      [&](graphics::CommandRecorder& recorder) {
        ASSERT_TRUE(second.Attach(recorder, Backend().GetResourceRegistry()));
        recorder.FlushBarriers();
        ASSERT_TRUE(readback->EnqueueCopy(recorder, *second.ProcessedCube(),
          { .src_slice = { .width = 16U, .height = 16U, .depth = 1U } }));
        ASSERT_TRUE(metadata_readback->EnqueueCopy(recorder, *second.Metadata(),
          { 0U, sizeof(environment::IblProductMetadata) }));
      });
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped);
    auto pixel = Pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    EXPECT_EQ(pixel, (Pixel { 2, 2, 2, 1 }));
    const auto mapped_metadata = metadata_readback->MapNow();
    ASSERT_TRUE(mapped_metadata);
    auto metadata = environment::IblProductMetadata {};
    std::memcpy(&metadata, mapped_metadata->Bytes().data(), sizeof(metadata));
    EXPECT_EQ(metadata.product_revision, second.Revision());
    EXPECT_EQ(metadata.processing_flags, 3U);
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    RecordProperty("retained_capture_revision", second.Revision());
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
          = forward_shader ? 0.04 : 0.08 * PackedSurfaceInputs()[1][1];
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

  NOLINT_TEST_F(IblSurfaceGpuTest, ImagesAgreeAcrossPublishedAndOffscreenPaths)
  {
    constexpr unsigned width = 128U;
    constexpr unsigned height = 96U;
    view.viewport.width = width;
    view.viewport.height = height;
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens);
    lens->get().SetAspectRatio(float(width) / height);
    lens->get().SetViewport(view.viewport);
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(CreateRegisteredTexture({
        .width = width,
        .height = height,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon,
      })));
    probe->prepare = [](RenderContext&) { };
    verify_manual_p = false;
    renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
      HdrPrecisionControl::kProduction);
    std::shared_ptr<const environment::internal::IblGpuProducts> products;
    probe->inspect = [&](const RenderContext& context, const auto&, unsigned) {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      products = RendererPublicationProbe::PublishedIblProducts(
        *owner, context.current_view.view_id);
    };
    const auto clear_inspection
      = ScopeGuard([&] noexcept { probe->inspect = {}; });
    char* destination = nullptr;
    std::size_t destination_size = 0;
    ASSERT_EQ(
      _dupenv_s(&destination, &destination_size, "OXYGEN_IBL_IMAGE"), 0);
    const auto owned_destination
      = std::unique_ptr<char, decltype(&std::free)>(destination, &std::free);
    const auto output = destination ? std::filesystem::path(destination)
                                    : std::filesystem::path {};
    if (!output.empty())
      std::filesystem::create_directories(output.parent_path());
    auto records = nlohmann::json::array();
    auto packed_references = nlohmann::json::array();
    const auto save
      = [&](const std::string& name, const std::vector<Pixel>& pixels) {
          if (output.empty())
            return;
          std::ofstream stream(output.parent_path()
              / (output.stem().string() + "-" + name + ".rgba32f"),
            std::ios::binary);
          ASSERT_TRUE(stream.good());
          stream.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size() * sizeof(Pixel)));
          ASSERT_TRUE(stream.good());
        };
    const auto render
      = [&](bool published, bool forward, float ev, unsigned count) {
          settings.manual_ev = ev;
          if (published) {
            // The published fixture expects resident geometry/materials. Warm
            // the shared uploads first; the measured frame still uses
            // publication.
            if (count > 1)
              ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, count));
            probe->color.reset();
            ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
          } else {
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, count));
          }
          ASSERT_NE(probe->color, nullptr);
        };
    const auto linear = [&]() {
      const auto domain = Read<FrameExposureData>(
        *probe->exposure->buffer, graphics::ResourceStates::kShaderResource);
      CHECK_F(std::isfinite(domain.pre_exposure) && domain.pre_exposure > 0);
      auto pixels = ReadFloatTexture(*probe->color);
      for (auto& pixel : pixels)
        for (unsigned channel = 0; channel < 3; ++channel)
          pixel[channel] /= domain.pre_exposure;
      return pixels;
    };
    double maximum_relative = 0;
    double maximum_display_peak_codes = 0;
    double maximum_display_rms_codes = 0;
    unsigned cases = 0;
    for (unsigned source = 0; source < 3; ++source) {
      if (source == 0) {
        Sky().SetCubemapResource(
          Texture(32U, true, [](auto face, auto x, auto y) {
            const auto direction = WorldDirection(face, x, y, 32U);
            return Pixel { .5F + .1F * direction.x, .5F + .1F * direction.y,
              .5F + .1F * direction.z, 1 };
          }));
      } else if (source == 1) {
        auto& fog = SetCapturedUniformFog();
        fog.SetFogInscatteringLuminance({ .25F, .5F, 1.0F });
      } else {
        auto& atmosphere = scene->GetEnvironment()
                             ->AddSystem<scene::environment::SkyAtmosphere>();
        atmosphere.SetEnabled(true);
        atmosphere.SetRenderInMainPass(false);
        const auto fog
          = scene->GetEnvironment()->TryGetSystem<scene::environment::Fog>();
        ASSERT_TRUE(fog);
        fog->SetFogDensity(.0001F);
        fog->SetHeightFalloffPerMeter(.001F);
        auto sun = scene->CreateNode("Image reference sun");
        auto light = std::make_unique<scene::DirectionalLight>();
        light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
        light->SetIntensityLux(100000.0F);
        ASSERT_TRUE(sun.AttachLight(std::move(light)));
      }
      Sky().SetDiffuseIntensity(1);
      Sky().SetSpecularIntensity(1);
      scene->NotifyEnvironmentAuthoringChange();
      // Keep every displayed channel away from clipping in all three sources.
      const float ev = source == 2 ? 9.0F : source == 1 ? 1.0F : 0.0F;
      for (const float metallic : { 0.0F, 1.0F }) {
        for (const float roughness : { .2F, .6F, 1.0F }) {
          const auto capture = source == 2 && metallic == 1 && roughness == .6F
            ? BeginOptionalCapture()
            : observer_ptr<graphics::FrameCaptureController> {};
          const auto finish_capture = ScopeGuard([&] noexcept {
            if (capture)
              EXPECT_TRUE(capture->EndCapture());
          });
          std::vector<Pixel> reference;
          std::vector<Pixel> forward_reference;
          std::array<std::vector<Pixel>, 2> deferred_display;
          std::array<Pixel, 3> packed {};
          for (const bool translucent : { false, true }) {
            for (const bool forward : { false, true }) {
              std::vector<Pixel> runtime_display;
              for (const bool published : { true, false }) {
                const auto name = std::to_string(source) + "-"
                  + std::to_string(int(metallic)) + "-"
                  + std::to_string(roughness) + "-"
                  + (translucent ? "alpha" : "opaque") + "-"
                  + (forward ? "forward" : "deferred") + "-"
                  + (published ? "runtime" : "offscreen");
                SCOPED_TRACE(name);
                CapturePackedInputs(!forward && !translucent);
                Material(translucent ? data::MaterialDomain::kAlphaBlended
                                     : data::MaterialDomain::kOpaque,
                  roughness, metallic, glm::vec3(1));
                Sky().SetIntensityMul(0);
                ASSERT_NO_FATAL_FAILURE(render(published, forward, ev, 5));
                const auto baseline = linear();
                Sky().SetIntensityMul(1);
                ASSERT_NO_FATAL_FAILURE(render(published, forward, ev, 1));
                const auto lit = linear();
                ASSERT_EQ(lit.size(), width * height);
                ASSERT_EQ(baseline.size(), lit.size());
                auto contribution = lit;
                const auto coverage = translucent ? .5F : 1.0F;
                double peak_error = 0;
                for (std::size_t i = 0; i < lit.size(); ++i) {
                  ASSERT_EQ(lit[i][3], coverage);
                  for (unsigned c = 0; c < 3; ++c) {
                    contribution[i][c]
                      = (lit[i][c] - baseline[i][c]) / coverage;
                    ASSERT_TRUE(std::isfinite(contribution[i][c]));
                    ASSERT_GT(contribution[i][c], 0);
                    if (!reference.empty()) {
                      const auto error = std::abs(
                        double(contribution[i][c]) - reference[i][c]);
                      peak_error = std::max(peak_error,
                        error / std::max(1.0e-5, double(reference[i][c])));
                    }
                  }
                }
                if (reference.empty()) {
                  reference = contribution;
                  packed = PackedSurfaceInputs();
                  EXPECT_EQ(packed[0], (Pixel { 1, 1, 1, 1 }));
                  EXPECT_FLOAT_EQ(packed[1][0], metallic);
                  EXPECT_NEAR(packed[1][2], roughness, 1.0e-6F);
                }
                for (auto& pixel : contribution)
                  pixel[3] = 1;
                if (forward || translucent) {
                  if (forward_reference.empty())
                    forward_reference = contribution;
                  else
                    EXPECT_EQ(contribution, forward_reference);
                } else {
                  EXPECT_EQ(contribution, reference);
                }
                maximum_relative = std::max(maximum_relative, peak_error);
                const auto display
                  = ReadFloatTexture(*framebuffer->GetDescriptor()
                      .color_attachments.front()
                      .texture);
                ASSERT_EQ(display.size(), width * height);
                for (const auto& pixel : display)
                  for (unsigned channel = 0; channel < 3; ++channel) {
                    ASSERT_TRUE(std::isfinite(pixel[channel]));
                    ASSERT_GT(pixel[channel], 0);
                    ASSERT_LT(pixel[channel], 1);
                  }
                double display_peak_codes = 0;
                double display_rms_codes = 0;
                if (!forward) {
                  deferred_display[translucent ? 1 : 0] = display;
                } else {
                  const auto& compared = deferred_display[translucent ? 1 : 0];
                  ASSERT_EQ(compared.size(), display.size());
                  double squared_codes = 0;
                  for (std::size_t i = 0; i < display.size(); ++i) {
                    for (unsigned c = 0; c < 3; ++c) {
                      ASSERT_TRUE(std::isfinite(display[i][c]));
                      ASSERT_TRUE(std::isfinite(compared[i][c]));
                      const double difference
                        = 255.0 * (double(display[i][c]) - compared[i][c]);
                      squared_codes += difference * difference;
                      display_peak_codes
                        = std::max(display_peak_codes, std::abs(difference));
                    }
                  }
                  display_rms_codes
                    = std::sqrt(squared_codes / (display.size() * 3U));
                  // A visual regression budget for different G-buffer inputs,
                  // in 8-bit code equivalents; FP16 filtering is checked below.
                  EXPECT_LE(display_peak_codes, 4.0);
                  EXPECT_LE(display_rms_codes, 1.0);
                  maximum_display_peak_codes
                    = std::max(maximum_display_peak_codes, display_peak_codes);
                  maximum_display_rms_codes
                    = std::max(maximum_display_rms_codes, display_rms_codes);
                }
                if (published)
                  runtime_display = display;
                else
                  EXPECT_EQ(display, runtime_display);
                ASSERT_NO_FATAL_FAILURE(save(name + "-baseline", baseline));
                ASSERT_NO_FATAL_FAILURE(save(name + "-lit", lit));
                ASSERT_NO_FATAL_FAILURE(save(name + "-display", display));
                records.push_back({ { "name", name }, { "coverage", coverage },
                  { "exposure_ev", ev },
                  { "maximum_relative_error", peak_error },
                  { "display_peak_code_equivalents", display_peak_codes },
                  { "display_rms_code_equivalents", display_rms_codes } });
                ++cases;
              }
            }
          }
          // Compare transport using the actual deferred shading normal. The
          // original images above retain the UNORM10 normal-packing difference.
          const float nx = packed[2][0] * 2 - 1;
          const float ny = packed[2][1] * 2 - 1;
          const auto normal = glm::normalize(
            glm::vec3(nx, ny, 1 - std::abs(nx) - std::abs(ny)));
          ASSERT_GT(normal.z, 0);
          const auto normal_texture = Texture(1U, false, [&](auto, auto, auto) {
            return Pixel { .5F + .5F * normal.x, .5F + .5F * normal.y,
              .5F + .5F * normal.z, 1 };
          });
          CapturePackedInputs(false);
          Material(data::MaterialDomain::kOpaque, roughness, metallic,
            glm::vec3(1), normal_texture);
          Sky().SetIntensityMul(0);
          ASSERT_NO_FATAL_FAILURE(render(false, true, ev, 5));
          const auto baseline = linear();
          Sky().SetIntensityMul(1);
          ASSERT_NO_FATAL_FAILURE(render(false, true, ev, 1));
          const auto lit = linear();
          ASSERT_EQ(lit.size(), reference.size());
          ASSERT_EQ(baseline.size(), reference.size());
          const auto f0_bound
            = metallic == 0 ? std::abs(.08 * packed[1][1] - .04) / .04 : 0.0;
          double maximum_residual = 0;
          for (std::size_t i = 0; i < lit.size(); ++i) {
            for (unsigned c = 0; c < 3; ++c) {
              const double matched = double(lit[i][c]) - baseline[i][c];
              ASSERT_TRUE(std::isfinite(matched));
              ASSERT_GT(matched, 0);
              const double error = std::abs(matched - reference[i][c]);
              maximum_residual = std::max(maximum_residual,
                error / std::max(1.0e-5, double(reference[i][c])));
            }
          }
          ASSERT_NE(products, nullptr);
          const auto comparison_products = products;
          const auto metadata = Read<environment::IblProductMetadata>(
            *products->metadata, graphics::ResourceStates::kShaderResource);
          auto canonical_metadata = metadata;
          canonical_metadata.precision_flags = 0;
          ASSERT_NO_FATAL_FAILURE(
            WriteComparisonMetadata(*comparison_products, canonical_metadata));
          const auto restore_metadata = ScopeGuard([&] noexcept {
            WriteComparisonMetadata(*comparison_products, metadata);
          });
          const auto canonical = [&](bool forward, content::ResourceKey normal,
                                   std::vector<Pixel>& result) {
            Material(data::MaterialDomain::kOpaque, roughness, metallic,
              glm::vec3(1), normal);
            Sky().SetIntensityMul(0);
            ASSERT_NO_FATAL_FAILURE(render(false, forward, ev, 5));
            const auto dark = linear();
            Sky().SetIntensityMul(1);
            ASSERT_NO_FATAL_FAILURE(render(false, forward, ev, 1));
            ASSERT_EQ(products.get(), comparison_products.get());
            result = linear();
            ASSERT_EQ(result.size(), dark.size());
            for (std::size_t i = 0; i < result.size(); ++i)
              for (unsigned c = 0; c < 3; ++c)
                result[i][c] -= dark[i][c];
          };
          std::vector<Pixel> canonical_deferred, canonical_forward;
          ASSERT_NO_FATAL_FAILURE(canonical(false, {}, canonical_deferred));
          ASSERT_NO_FATAL_FAILURE(
            canonical(true, normal_texture, canonical_forward));
          ASSERT_EQ(canonical_deferred.size(), reference.size());
          ASSERT_EQ(canonical_forward.size(), reference.size());
          double canonical_residual = 0, filter_rgb = 0, filter_ev = 0;
          for (std::size_t i = 0; i < reference.size(); ++i) {
            for (unsigned c = 0; c < 3; ++c) {
              const double d = canonical_deferred[i][c];
              const double f = canonical_forward[i][c];
              ASSERT_TRUE(std::isfinite(d) && std::isfinite(f));
              ASSERT_GT(d, 0);
              ASSERT_GT(f, 0);
              canonical_residual
                = std::max(canonical_residual, std::abs(d - f) / d);
              for (const auto pair :
                { std::array<double, 2> { reference[i][c], d },
                  std::array<double, 2> {
                    double(lit[i][c]) - baseline[i][c], f } }) {
                filter_rgb
                  = std::max(filter_rgb, std::abs(pair[0] - pair[1]) / pair[1]);
                filter_ev
                  = std::max(filter_ev, std::abs(std::log2(pair[0] / pair[1])));
              }
            }
          }
          // Keep the normal/F0-matched residual as diagnostic evidence. Its
          // small transport/sampling differences are not a filtering budget.
          EXPECT_LE(filter_rgb, .0025);
          EXPECT_LE(filter_ev, 2.0 / 1024.0);
          const auto name = std::to_string(source) + "-"
            + std::to_string(int(metallic)) + "-" + std::to_string(roughness)
            + "-packed-normal-reference";
          ASSERT_NO_FATAL_FAILURE(save(name + "-baseline", baseline));
          ASSERT_NO_FATAL_FAILURE(save(name + "-lit", lit));
          ASSERT_NO_FATAL_FAILURE(
            save(name + "-canonical-deferred", canonical_deferred));
          ASSERT_NO_FATAL_FAILURE(
            save(name + "-canonical-forward", canonical_forward));
          packed_references.push_back(
            { { "name", name }, { "packed_inputs", packed },
              { "normal", { normal.x, normal.y, normal.z } },
              { "f0_relative_bound", f0_bound },
              { "maximum_residual", maximum_residual },
              { "canonical_residual", canonical_residual },
              { "filter_relative_rgb", filter_rgb },
              { "filter_stops", filter_ev } });
        }
      }
    }
    if (!output.empty()) {
      std::ofstream manifest(output);
      ASSERT_TRUE(manifest.good());
      manifest << nlohmann::json {
        { "width", width }, { "height", height }, { "cases", records },
        { "packed_normal_references", packed_references },
        { "maximum_relative_error", maximum_relative },
        { "maximum_display_peak_code_equivalents", maximum_display_peak_codes },
        { "maximum_display_rms_code_equivalents", maximum_display_rms_codes },
        { "display_limits",
          { { "peak_code_equivalents", 4 }, { "rms_code_equivalents", 1 } } }
      }.dump(2);
      ASSERT_TRUE(manifest.good());
    }
    RecordProperty("image_cases", cases);
    RecordProperty("maximum_image_relative_error", maximum_relative);
  }

} // namespace
} // namespace oxygen::vortex::testing::exposure
