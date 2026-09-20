//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Cooker/Import/Internal/TextureCooker.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::vortex::testing::exposure {

using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;

NOLINT_TEST_F(
  ExposureGpuTest, MaterialTexturesPreserveTypedLinearRadianceInScenePaths)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
  owned_test_engine_
    = std::make_unique<::testing::NiceMock<ExposureTestEngine>>();
  ON_CALL(*owned_test_engine_, GetAssetLoader())
    .WillByDefault(::testing::Return(observer_ptr<content::IAssetLoader> {
      owned_asset_loader_.get(),
    }));
  ASSERT_TRUE(renderer_->OnAttached(observer_ptr<IAsyncEngine> {
    owned_test_engine_.get(),
  }));
  auto scene = std::make_shared<scene::Scene>("Material producer domain", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1);
  post.SetBloomIntensity(0);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = {
    .width = 1,
    .height = 1,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto mesh_node = scene->CreateNode("Radiance triangle");
  std::vector<data::Vertex> vertices(3);
  const std::array positions {
    glm::vec3 {
      -2,
      -2,
      -1,
    },
    glm::vec3 {
      2,
      -2,
      -1,
    },
    glm::vec3 {
      0,
      2,
      -1,
    },
  };
  for (unsigned i = 0; i < 3; ++i) {
    vertices.at(i) = { .position = positions.at(i),
      .normal = { 0, 0, 1, },
      .texcoord = { .5F, .5F, },
      .tangent = { 1, 0, 0, },
      .bitangent = { 0, 1, 0, },
      .color = { 1, 1, 1, 1, }, };
  }
  std::shared_ptr<data::Mesh> mesh
    = data::MeshBuilder()
        .WithVertices(vertices)
        .WithIndices(std::vector<std::uint32_t> {
          0,
          1,
          2,
        })
        .BeginSubMesh("Radiance", data::MaterialAsset::CreateDefault())
        .WithMeshView({
          .first_index = 0,
          .index_count = 3,
          .first_vertex = 0,
          .vertex_count = 3,
        })
        .EndSubMesh()
        .Build();
  data::pak::geometry::GeometryAssetDesc geometry_desc {};
  geometry_desc.lod_count = 1;
  geometry_desc.bounding_box_min[0] = geometry_desc.bounding_box_min[1] = -2;
  geometry_desc.bounding_box_max[0] = geometry_desc.bounding_box_max[1] = 2;
  geometry_desc.bounding_box_min[2] = geometry_desc.bounding_box_max[2] = -1;
  mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath("/Test/Exposure/Domain.ogeo"),
    geometry_desc,
    std::vector<std::shared_ptr<data::Mesh>> {
      mesh,
    }));
  auto output = CreateRegisteredTexture({
    .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  struct Probe final : IViewExtension {
    observer_ptr<Renderer> renderer;
    std::shared_ptr<const Texture> color;
    explicit Probe(Renderer& value)
      : renderer(&value)
    {
    }
    postprocess::ExposurePass::FrameLease exposure;
    unsigned draws = 0;
    std::function<void(RenderContext&)> prepare;
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      prepare(hook.render_context);
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      exposure = hook.render_context.current_view.frame_exposure;
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer);
      const auto& extracted
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      color = extracted.valid ? extracted.texture->shared_from_this() : nullptr;

      const auto frame = hook.render_context.current_view.prepared_frame;
      draws = frame ? static_cast<unsigned>(frame->draw_metadata_bytes.size()
                        / sizeof(DrawMetadata))
                    : 0;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  probe->prepare = [&](RenderContext& ctx) -> void {
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(SubmitCommands("Vortex Exposure Frame",
                [&](graphics::CommandRecorder& recorder) -> auto {
                  return service->PrepareFrameExposure(ctx, recorder, false);
                }),
      nullptr);
  };

  renderer_->RegisterViewExtension(probe);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  unsigned sequence = 0;
  unsigned case_count = 0;
  namespace cook = content::import;
  struct Sample {
    Format source_format;
    ColorSpace source_space;
    Format stored_format;
    float source_value;
    double linear_value;
    bool range_failure;
  };
  std::vector<Sample> samples;
  for (const float value : {
         0.0F,
         0x1p-24F,
         .25F,
         131072.0F,
         0x1p32F,
         0x1p33F,
       }) {
    samples.push_back({
      .source_format = Format::kRGBA32Float,
      .source_space = ColorSpace::kLinear,
      .stored_format = Format::kRGBA32Float,
      .source_value = value,
      .linear_value = value,
      .range_failure = value > 0x1p32F,
    });
  }
  for (const float value : {
         0x1p-24F,
         .25F,
         65504.0F,
       }) {
    samples.push_back({
      .source_format = Format::kRGBA32Float,
      .source_space = ColorSpace::kLinear,
      .stored_format = Format::kRGBA16Float,
      .source_value = value,
      .linear_value = value,
      .range_failure = false,
    });
  }
  const auto decode = [](double x) -> double {
    return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
  };
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kSRGB,
    .stored_format = Format::kRGBA32Float,
    .source_value = 128,
    .linear_value = decode(128.0 / 255),
    .range_failure = false,
  });
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kSRGB,
    .stored_format = Format::kRGBA16Float,
    .source_value = 128,
    .linear_value = .2158203125,
    .range_failure = false,
  });
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kSRGB,
    .stored_format = Format::kRGBA8UNormSRGB,
    .source_value = 128,
    .linear_value = decode(128.0 / 255),
    .range_failure = false,
  });
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kLinear,
    .stored_format = Format::kRGBA32Float,
    .source_value = 128,
    .linear_value = 128.0 / 255,
    .range_failure = false,
  });
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kLinear,
    .stored_format = Format::kRGBA8UNorm,
    .source_value = 128,
    .linear_value = 128.0 / 255,
    .range_failure = false,
  });
  samples.push_back({
    .source_format = Format::kRGBA8UNorm,
    .source_space = ColorSpace::kLinear,
    .stored_format = Format::kRGBA8UNormSRGB,
    .source_value = 128,
    .linear_value = decode(188.0 / 255),
    .range_failure = false,
  });
  for (const bool forward : {
         false,
         true,
       }) {
    for (const bool unlit : {
           false,
           true,
         }) {
      for (const auto domain : {
             data::MaterialDomain::kOpaque,
             data::MaterialDomain::kMasked,
             data::MaterialDomain::kAlphaBlended,
           }) {
        for (const auto& sample : samples) {
          for (const float ev : {
                 -32.0F,
                 0.0F,
                 32.0F,
               }) {
            // Exercise the full P interval with exact float inputs; encoding
            // controls need one P because their cooked source is unchanged.
            if (ev != 0
              && (sample.source_format != Format::kRGBA32Float
                || sample.stored_format != Format::kRGBA32Float)) {
              continue;
            }
            SCOPED_TRACE(forward);
            SCOPED_TRACE(unlit);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(static_cast<int>(sample.stored_format));
            SCOPED_TRACE(sample.source_value);
            SCOPED_TRACE(ev);
            probe->color.reset();
            probe->draws = 0;
            auto image = cook::ScratchImage::Create({
              .width = 1,
              .height = 1,
              .format = sample.source_format,
            });
            if (sample.source_format == Format::kRGBA32Float) {
              const Pixel pixel {
                sample.source_value,
                sample.source_value,
                sample.source_value,
                1,
              };
              std::memcpy(image.GetMutablePixels(0, 0).data(), pixel.data(),
                sizeof(pixel));
            } else {
              const auto value = static_cast<std::uint8_t>(sample.source_value);
              const std::array<std::uint8_t, 4> pixel {
                value,
                value,
                value,
                255,
              };
              std::memcpy(image.GetMutablePixels(0, 0).data(), pixel.data(),
                sizeof(pixel));
            }
            auto import = cook::TextureImportDesc {};
            import.intent = cook::TextureIntent::kEmissive;
            import.source_color_space = sample.source_space;
            import.output_format = sample.stored_format;
            import.mip_policy = cook::MipPolicy::kNone;
            const auto cooked = cook::CookTexture(
              std::move(image), import, cook::D3D12PackingPolicy::Instance());
            ASSERT_TRUE(cooked.has_value()) << static_cast<int>(cooked.error());
            data::pak::core::TextureResourceDesc texture_desc {};
            texture_desc.data_offset = sizeof(texture_desc);
            texture_desc.size_bytes
              = static_cast<data::pak::core::DataBlobSizeT>(
                cooked->payload.size());
            texture_desc.texture_type
              = static_cast<std::uint8_t>(cooked->desc.texture_type);
            texture_desc.width = cooked->desc.width;
            texture_desc.height = cooked->desc.height;
            texture_desc.depth = cooked->desc.depth;
            texture_desc.array_layers = cooked->desc.array_layers;
            texture_desc.mip_levels = cooked->desc.mip_levels;
            texture_desc.format
              = static_cast<std::uint8_t>(cooked->desc.format);
            texture_desc.alignment = 256;
            texture_desc.content_hash = cooked->desc.content_hash;
            std::vector<std::uint8_t> payload(
              sizeof(texture_desc) + cooked->payload.size());
            std::memcpy(payload.data(), &texture_desc, sizeof(texture_desc));
            std::memcpy(
              std::span {
                payload,
              }
                .subspan(sizeof(texture_desc), cooked->payload.size())
                .data(),
              cooked->payload.data(), cooked->payload.size());
            const auto key = owned_asset_loader_->PreloadCookedTexture(payload);
            const bool base_color_source = forward && unlit;
            const float coverage
              = domain == data::MaterialDomain::kAlphaBlended ? .5F : 1.0F;
            data::pak::render::MaterialAssetDesc material_desc {};
            material_desc.material_domain = static_cast<std::uint8_t>(domain);
            material_desc.flags = data::pak::render::kMaterialFlag_DoubleSided;
            if (unlit) {
              material_desc.flags |= data::pak::render::kMaterialFlag_Unlit;
            }
            if (domain == data::MaterialDomain::kMasked) {
              material_desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
            }
            std::ranges::fill(
              std::span {
                material_desc.base_color,
              }
                .first<3>(),
              base_color_source ? 1.0F : 0.0F);
            material_desc.base_color[3] = coverage;
            material_desc.normal_scale = 1;
            material_desc.roughness = data::Unorm16 {
              1,
            };
            material_desc.ambient_occlusion = data::Unorm16 {
              1,
            };
            material_desc.uv_scale[0] = material_desc.uv_scale[1] = 1;
            for (auto& v : material_desc.emissive_factor) {
              v = data::HalfFloat {
                base_color_source ? 0.0F : 1.0F,
              };
            }
            std::vector<content::ResourceKey> keys(6);
            keys.at(base_color_source ? 0 : 5) = key;
            auto material = std::make_shared<data::MaterialAsset>(
              data::AssetKey::FromVirtualPath(
                "/Test/Exposure/Domain" + std::to_string(case_count) + ".omat"),
              material_desc, std::vector<data::ShaderReference> {}, keys);
            mesh_node.GetRenderable().SetMaterialOverride(0, 0, material);
            settings.manual_ev = ev;
            post.SetExposureSettings(settings);
            scene->Update();
            for (unsigned warmup = 0; warmup < 5; ++warmup) {
              const auto slot = frame::Slot {
                sequence % 3,
              };
              Backend().BeginFrame(
                frame::SequenceNumber {
                  sequence + 1,
                },
                slot);
              frame.SetFrameSlot(
                slot, engine::internal::EngineTagFactory::Get());
              frame.SetFrameSequenceNumber(
                frame::SequenceNumber {
                  ++sequence,
                },
                engine::internal::EngineTagFactory::Get());
              renderer_->OnFrameStart(observer_ptr {
                &frame,
              });
              auto facade = renderer_->ForOffscreenScene();
              facade.SetFrameSession({ .frame_slot = slot,
                .frame_sequence = frame::SequenceNumber { sequence, },
                .delta_time_seconds = 0, });
              facade.SetSceneSource({ .scene = observer_ptr { scene.get(), }, });
              facade.SetViewIntent(
                Renderer::OffscreenSceneViewInput::FromCamera("Domain",
                  ViewId {
                    100U,
                  },
                  view, camera)
                  .SetViewStateHandle(CompositionView::ViewStateHandle {
                    100U,
                  }));
              facade.SetOutputTarget(
                { .framebuffer = observer_ptr { framebuffer.get(), }, });
              facade.SetPipeline(forward
                  ? Renderer::OffscreenPipelineInput::Forward()
                  : Renderer::OffscreenPipelineInput::Deferred());
              auto session = facade.Finalize();
              if (!session.has_value()) {
                FAIL() << "Expected session to contain a value";
              }
              ASSERT_TRUE(session->ExecuteInsideFrame(frame));
              renderer_->OnFrameEnd(observer_ptr {
                &frame,
              });
              Backend().EndFrame(
                frame::SequenceNumber {
                  sequence,
                },
                slot);
              WaitForQueueIdle();
            }
            ASSERT_EQ(probe->draws, 1U);
            ASSERT_NE(probe->color, nullptr);
            ASSERT_NE(probe->exposure, nullptr);
            const auto domain_data = Read<FrameExposureData>(
              *probe->exposure->buffer, ResourceStates::kShaderResource);
            const double p = std::exp2(-static_cast<double>(ev));
            EXPECT_EQ(domain_data.pre_exposure, p);
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            const double expected = sample.linear_value * p * coverage;
            for (unsigned c = 0; c < 3; ++c) {
              if (sample.stored_format == Format::kRGBA8UNormSRGB) {
                // D3D 3.2.3.7 permits half a code of error in encoded sRGB
                // space. Check that bound and the frozen PBR image budget.
                // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
                const double encoded = sample.linear_value <= .0031308
                  ? sample.linear_value * 12.92
                  : (1.055 * std::pow(sample.linear_value, 1.0 / 2.4)) - .055;
                const double code = std::round(encoded * 255);
                const double arithmetic
                  = (std::abs(expected) * 2e-5) + 0x1p-120;
                EXPECT_GE(pixels.at(0).at(c),
                  (decode(std::max(0.0, code - .5) / 255) * p * coverage)
                    - arithmetic);
                EXPECT_LE(pixels.at(0).at(c),
                  (decode(std::min(255.0, code + .5) / 255) * p * coverage)
                    + arithmetic);
                EXPECT_NEAR(pixels.at(0).at(c) / p,
                  sample.linear_value * coverage,
                  (.005 * sample.linear_value * coverage) + 2e-5);
              } else {
                EXPECT_NEAR(pixels.at(0).at(c), expected,
                  (std::abs(expected) * 2e-5) + 0x1p-120);
              }
            }
            EXPECT_FLOAT_EQ(pixels.at(0).at(3), coverage);
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (sample.range_failure) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended ? 4U
                  : forward                                   ? 4U
                                                              : 1U);
            } else {
              EXPECT_EQ(status.flags & 16U, 0U);
            }
            ++case_count;
          }
        }
      }
    }
  }
  probe->prepare = {};
  RecordProperty("material_endpoint_cases", case_count);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyUploadKeepsHalfAndFloatStorageCoherentAcrossFacesAndMips)
{
  for (const bool wide : {
         false,
         true,
       }) {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
    desc.width = desc.height = 2U;
    desc.depth = 1U;
    desc.array_layers = 6U;
    desc.mip_levels = 1U;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256U;
    desc.content_hash = wide ? 2U : 1U;
    std::vector<std::uint8_t> data_region(6ULL * 4U * sizeof(Pixel));
    std::vector<data::pak::render::SubresourceLayout> layouts;
    std::array<Pixel, 6U> colors {};
    for (unsigned face = 0U; face < 6U; ++face) {
      colors.at(face) = {
        wide ? 0x1p30F : .5F,
        wide ? 0x1p-24F : .25F,
        .25F + (static_cast<float>(face) * .125F),
        1.0F,
      };
      for (unsigned pixel = 0U; pixel < 4U; ++pixel) {
        std::memcpy(
          std::span {
            data_region,
          }
            .subspan(((face * 4U) + pixel) * sizeof(Pixel), sizeof(Pixel))
            .data(),
          colors.at(face).data(), sizeof(Pixel));
      }
      layouts.push_back({
        .offset_bytes = face * 64U,
        .row_pitch_bytes = 32U,
        .size_bytes = 64U,
      });
    }
    auto payload = vortex::testing::detail::BuildV4TexturePayload(
      desc, layouts, data_region);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    auto source = data::TextureResource(desc, std::move(payload));
    auto model = environment::SkyLightEnvironmentModel {};
    model.enabled = true;
    model.source = environment::kSkyLightSourceSpecifiedCubemap;
    model.cubemap_resource = content::ResourceKey {
      wide ? 502U : 501U,
    };
    model.lower_hemisphere_is_solid_color = false;
    auto processor = environment::internal::IblProcessor(*renderer_);
    const auto first
      = processor.RefreshStaticSkyLightProducts({}, model, &source);
    auto texture = FailureBackend().processed_sky.lock();
    ASSERT_NE(texture, nullptr);
    ASSERT_EQ(texture->GetDescriptor().format,
      wide ? Format::kRGBA32Float : Format::kRGBA16Float);
    ASSERT_EQ(texture->GetDescriptor().mip_levels, 2U);
    WaitForQueueIdle();
    renderer_->GetUploadCoordinator().OnFrameStart(
      vortex::internal::RendererTagFactory::Get(),
      frame::Slot {
        wide ? 1U : 0U,
      });
    const auto ready = processor.RefreshStaticSkyLightProducts(
      first.probe_state, model, &source);
    ASSERT_TRUE(ready.probe_state.valid);
    const auto scale = ready.probe_state.static_sky_light.source_radiance_scale;
    for (unsigned face = 0U; face < 6U; ++face) {
      for (unsigned mip = 0U; mip < 2U; ++mip) {
        auto readback
          = GetReadbackManager()->CreateTextureReadback("Processed sky texel");
        {
          auto recorder = AcquireRecorder("Processed sky readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = 1U,
                    .height = 1U,
                    .depth = 1U,
                    .mip_level = mip,
                    .array_slice = face, }, })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        if (!mapped.has_value()) {
          FAIL() << "Expected mapped to contain a value";
        }
        Pixel pixel {};
        if (wide) {
          std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
        } else {
          std::array<std::uint16_t, 4U> packed {};
          std::memcpy(packed.data(), mapped->Data(), sizeof(packed));
          for (unsigned channel = 0U; channel < 4U; ++channel) {
            pixel.at(channel)
              = data::HalfFloat { packed.at(channel), }.ToFloat();
          }
        }
        for (unsigned channel = 0U; channel < 3U; ++channel) {
          EXPECT_NEAR(static_cast<double>(pixel.at(channel)) * scale,
            colors.at(face).at(channel),
            (std::abs(static_cast<double>(colors.at(face).at(channel))) * 2e-5)
              + 0x1p-120);
        }
        EXPECT_EQ(pixel.at(3), 1.0F);
      }
    }
    FlushBackend();
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyIntensityEditPromotesBeforePublishingAmplifiedHalfLoss)
{
  data::pak::core::TextureResourceDesc desc {};
  desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
  desc.width = desc.height = desc.depth = 1U;
  desc.array_layers = 6U;
  desc.mip_levels = 1U;
  desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
  desc.alignment = 256U;
  desc.content_hash = 99U;
  std::vector<std::uint8_t> bytes(6U * sizeof(Pixel));
  std::vector<data::pak::render::SubresourceLayout> layouts;
  const Pixel color {
    0x1p-50F,
    0x1p-50F,
    0x1p-50F,
    1.0F,
  };
  for (unsigned face = 0U; face < 6U; ++face) {
    std::memcpy(
      std::span {
        bytes,
      }
        .subspan(face * sizeof(Pixel), sizeof(Pixel))
        .data(),
      color.data(), sizeof(Pixel));
    layouts.push_back({
      .offset_bytes = face * 16U,
      .row_pitch_bytes = 16U,
      .size_bytes = 16U,
    });
  }
  auto payload
    = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  auto source = data::TextureResource(desc, std::move(payload));
  auto model = environment::SkyLightEnvironmentModel {};
  model.enabled = true;
  model.source = environment::kSkyLightSourceSpecifiedCubemap;
  model.cubemap_resource = content::ResourceKey {
    511U,
  };
  model.lower_hemisphere_is_solid_color = false;
  auto processor = environment::internal::IblProcessor(*renderer_);
  auto state
    = processor.RefreshStaticSkyLightProducts({}, model, &source).probe_state;
  auto half = FailureBackend().processed_sky.lock();
  ASSERT_NE(half, nullptr);
  EXPECT_EQ(half->GetDescriptor().format, Format::kRGBA16Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(),
    frame::Slot {
      0U,
    });
  state = processor.RefreshStaticSkyLightProducts(state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  const auto original_key = state.static_sky_light.key;
  const auto original_revision = state.static_sky_light.product_revision;
  model.intensity_mul = 2.0F;
  const auto harmless
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(harmless.refreshed);
  EXPECT_EQ(
    harmless.probe_state.static_sky_light.product_revision, original_revision);
  EXPECT_EQ(FailureBackend().processed_sky.lock(), half);
  model.intensity_mul = 0x1p50F;
  const auto pending = processor.RefreshStaticSkyLightProducts(
    harmless.probe_state, model, &source);
  EXPECT_FALSE(pending.probe_state.valid);
  EXPECT_EQ(pending.probe_state.static_sky_light.processed_cubemap_srv,
    kInvalidShaderVisibleIndex);
  auto full = FailureBackend().processed_sky.lock();
  ASSERT_NE(full, nullptr);
  EXPECT_NE(full, half);
  EXPECT_EQ(full->GetDescriptor().format, Format::kRGBA32Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(),
    frame::Slot {
      1U,
    });
  state = processor
            .RefreshStaticSkyLightProducts(pending.probe_state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  EXPECT_EQ(state.static_sky_light.key, original_key);
  EXPECT_GT(state.static_sky_light.product_revision, original_revision);
  auto service = EnvironmentLightingService(*renderer_);
  const auto published
    = vortex::testing::RendererPublicationProbe::BuildStaticSkyPublication(
      service, ctx_, state, model);
  EXPECT_EQ(published.sky_light.radiance_scale, 0x1p50F);
  auto readback
    = GetReadbackManager()->CreateTextureReadback("Promoted sky pixel");
  {
    auto recorder = AcquireRecorder("Promoted sky readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*full));
    ASSERT_TRUE(readback
        ->EnqueueCopy(*recorder, *full,
          { .src_slice = { .width = 1U, .height = 1U, .depth = 1U, }, })
        .has_value());
  }
  const auto mapped = readback->MapNow();
  if (!mapped.has_value()) {
    FAIL() << "Expected mapped to contain a value";
  }
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  for (unsigned channel = 0U; channel < 3U; ++channel) {
    EXPECT_EQ(actual.at(channel) * published.sky_light.radiance_scale, 1.0F);
  }
  model.intensity_mul = 1.0F;
  const auto dimmed
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(dimmed.refreshed);
  EXPECT_EQ(dimmed.probe_state.static_sky_light.product_revision,
    state.static_sky_light.product_revision);
  EXPECT_EQ(FailureBackend().processed_sky.lock(), full);
  FlushBackend();
}

} // namespace oxygen::vortex::testing::exposure
