//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;

auto ExposureLightingGpuTest::AdditionalCapabilities() const -> CapabilitySet
{
  return RendererCapabilityFamily::kNone;
}

ExposureLightingGpuTest::Probe::Probe(Renderer& value)
  : renderer(value)
{
}

auto ExposureLightingGpuTest::Probe::OnViewSetup(const ViewSetupContext& hook)
  -> void
{
  prepare(hook.render_context);
}

auto ExposureLightingGpuTest::Probe::OnPostRenderViewGpu(
  const ViewRenderGpuContext& hook) -> void
{
  exposure = hook.render_context.current_view.frame_exposure;
  auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
  const auto& extracted = owner->GetSceneTextureExtracts().resolved_scene_color;
  color = extracted.valid ? extracted.texture->shared_from_this() : nullptr;

  const auto prepared = hook.render_context.current_view.prepared_frame;
  draws = prepared ? static_cast<unsigned>(prepared->draw_metadata_bytes.size()
                       / sizeof(DrawMetadata))
                   : 0;
  raster_depths.clear();
  early_depth_complete
    = hook.render_context.current_view.IsEarlyDepthComplete();
  if (prepared) {
    for (const auto& draw :
      vortex::testing::RendererPublicationProbe::BasePassDrawCommands(*owner)) {
      if (draw.draw_index >= prepared->GetDrawMetadata().size()) {
        continue;
      }
      const auto material = prepared->GetDrawMetadata()
                              .subspan(draw.draw_index, 1)
                              .front()
                              .material_handle;
      const auto item = std::ranges::find_if(
        prepared->render_items, [material](const auto& value) -> auto {
          return value.material_handle.get() == material;
        });
      ASSERT_NE(item, prepared->render_items.end());
      raster_depths.push_back(item->world_bounding_sphere.z + 1.0F);
    }
  }
  if (inspect) {
    inspect(hook.render_context, extracted, draws);
  }
}

auto ExposureLightingGpuTest::SetUp() -> void
{
  ExposureGpuTest::SetUp();
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition
      | RendererCapabilityFamily::kEnvironmentLighting
      | AdditionalCapabilities());
  // These fixtures exercise the certified producer/consumer contracts. Normal
  // production selection is tested explicitly and selected by benchmark
  // recipes.
  renderer_->GetDiagnosticsService().SetHdrPrecisionControl(
    HdrPrecisionControl::kQualified);
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
  renderer_->RegisterConsoleBindings(observer_ptr {
    &fixture_console,
  });
  ASSERT_EQ(fixture_console.Execute("vtx.occlusion.enable false").status,
    console::ExecutionStatus::kOk);
  scene = std::make_shared<scene::Scene>("Lighting producer domain", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1);
  post.SetBloomIntensity(0);
  camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  view = View {};
  view.viewport = {
    .width = 1,
    .height = 1,
  };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  mesh_node = scene->CreateNode("Radiance triangle");
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
  framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  probe = std::make_shared<Probe>(*renderer_);
  probe->prepare = [&](RenderContext& ctx) -> void {
    ctx.current_view.depth_prepass_mode = depth_mode;
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
  };

  renderer_->RegisterViewExtension(probe);
  frame.SetScene(observer_ptr {
    scene.get(),
  });
}

auto ExposureLightingGpuTest::MakeEmissiveMaterial(float value)
  -> std::shared_ptr<data::MaterialAsset>
{
  data::pak::core::TextureResourceDesc desc {};
  desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
  desc.width = desc.height = desc.depth = desc.mip_levels = desc.array_layers
    = 1;
  desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
  desc.alignment = 256;
  const auto key = owned_asset_loader_->MintSyntheticTextureKey();
  desc.content_hash = key.get();
  const Pixel pixel {
    value,
    value,
    value,
    1,
  };
  std::vector<std::uint8_t> bytes(sizeof(pixel));
  std::memcpy(bytes.data(), pixel.data(), sizeof(pixel));
  const std::array layouts {
    data::pak::render::SubresourceLayout {
      .offset_bytes = 0,
      .row_pitch_bytes = sizeof(pixel),
      .size_bytes = sizeof(pixel),
    },
  };
  auto payload
    = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  owned_asset_loader_->SetTexture(
    key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
  data::pak::render::MaterialAssetDesc authored {};
  authored.flags = data::pak::render::kMaterialFlag_DoubleSided;
  authored.base_color[3] = 1;
  authored.normal_scale = 1;
  authored.roughness = data::Unorm16 {
    1,
  };
  authored.ambient_occlusion = data::Unorm16 {
    1,
  };
  authored.uv_scale[0] = authored.uv_scale[1] = 1;
  for (auto& component : authored.emissive_factor) {
    component = data::HalfFloat {
      1.0F,
    };
  }
  std::vector<content::ResourceKey> keys(6);
  keys.at(5) = key;
  return std::make_shared<data::MaterialAsset>(
    data::AssetKey::FromVirtualPath(
      "/Test/Exposure/Wide" + std::to_string(key.get()) + ".omat"),
    authored, std::vector<data::ShaderReference> {}, keys);
}

auto ExposureLightingGpuTest::UniformReferenceGain(float luminance) const
  -> double
{
  // Independent one-pixel, full-percentile histogram oracle. Both adjacent
  // bins retain their rounded share of the fixed 4095 sample mass.
  if (luminance <= std::exp2(settings.min_log_luminance)) {
    return std::exp2(
             -static_cast<double>(settings.min_ev) + settings.compensation_ev)
      * (settings.target_luminance / .18) * (settings.key / 12.5);
  }
  const double bin = std::clamp((std::log2(static_cast<double>(luminance))
                                  - settings.min_log_luminance)
                         / settings.log_luminance_range,
                       0.0, 1.0)
    * 255;
  const double lower = std::floor(bin);
  const double upper_mass = std::floor(((bin - lower) * 4095) + .5);
  const double measured_log = settings.min_log_luminance
    + ((lower + (upper_mass / 4095)) * settings.log_luminance_range / 255);
  const double ev = std::clamp(measured_log - std::log2(.18),
    static_cast<double>(settings.min_ev), static_cast<double>(settings.max_ev));
  return std::exp2(-ev + settings.compensation_ev)
    * (settings.target_luminance / .18) * (settings.key / 12.5);
}

auto ExposureLightingGpuTest::ExpectSurfaceExposure(float luminance,
  double expected_gain, const Texture& reference, ExposureStateData& state)
  -> void
{
  state = Read<ExposureStateData>(
    *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
  const auto domain = Read<FrameExposureData>(
    *probe->exposure->buffer, ResourceStates::kShaderResource);
  EXPECT_TRUE(std::isfinite(domain.pre_exposure));
  EXPECT_GT(domain.pre_exposure, 0);
  EXPECT_GT(state.latent_scale, 0);
  if (expected_gain == 0) {
    EXPECT_EQ(state.displayed_scale, 0);
  } else {
    ASSERT_GT(state.displayed_scale, 0);
    EXPECT_NEAR(
      std::log2(double(state.displayed_scale)), std::log2(expected_gain), 4e-4);
  }
  if (luminance >= 0) {
    const auto hdr = ReadFloatTexture(reference);
    ASSERT_EQ(hdr.size(), 1U);
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_NEAR(static_cast<double>(hdr.at(0).at(c)) / domain.pre_exposure,
        luminance, (static_cast<double>(luminance) * 2e-5) + 0x1p-120);
    }
  }
  const auto mapped = ReadFloatTexture(
    *framebuffer->GetDescriptor().color_attachments.front().texture);
  ASSERT_EQ(mapped.size(), 1U);
  const double expected = std::clamp(
    std::clamp(static_cast<double>(luminance) * expected_gain, 0.0, 1.0)
      - (.5 / 255),
    0.0, 1.0);
  for (unsigned c = 0; c < 3; ++c) {
    EXPECT_NEAR(mapped.at(0).at(c), expected, 2e-4);
  }
  EXPECT_EQ(mapped.at(0).at(3), 1);
}

auto ExposureLightingGpuTest::ReferenceAdaptedGain(
  // The independent adaptation equation takes previous gain, target gain,
  // then elapsed seconds.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  double previous, double target, double seconds) const -> double
{
  const double q = std::log2(previous);
  const double destination = std::log2(target);
  const double radius = std::abs(destination - q);
  const double speed
    = target < previous ? settings.speed_up : settings.speed_down;
  if (radius == 0 || speed == 0 || seconds == 0) {
    return previous;
  }
  const double distance = settings.transition_distance;
  const double crossing = std::max(radius - distance, 0.0) / speed;
  const double remaining = seconds <= crossing ? radius - (speed * seconds)
                                               : std::min(radius, distance)
      * std::exp(-speed * (seconds - crossing) / distance);
  return std::exp2(destination - ((destination > q ? 1 : -1) * remaining));
}

auto ExposureLightingGpuTest::SetSurface(
  data::MaterialDomain domain, float emission, bool rejected_mask) -> void
{
  data::pak::render::MaterialAssetDesc desc {};
  desc.material_domain = static_cast<std::uint8_t>(domain);
  desc.flags = data::pak::render::kMaterialFlag_NoTextureSampling
    | data::pak::render::kMaterialFlag_DoubleSided;
  if (domain == data::MaterialDomain::kMasked) {
    desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
  }
  desc.base_color[0] = desc.base_color[1] = desc.base_color[2] = 1;
  if (rejected_mask) {
    desc.base_color[3] = 0.0F;
  } else {
    desc.base_color[3]
      = domain == data::MaterialDomain::kAlphaBlended ? .5F : 1.0F;
  }
  for (auto& value : desc.emissive_factor) {
    value = data::HalfFloat {
      emission,
    };
  }
  desc.normal_scale = 1;
  desc.roughness = data::Unorm16 {
    1,
  };
  desc.ambient_occlusion = data::Unorm16 {
    1,
  };
  desc.uv_scale[0] = desc.uv_scale[1] = 1;
  mesh_node.GetRenderable().SetMaterialOverride(0, 0,
    std::make_shared<data::MaterialAsset>(
      data::AssetKey::FromVirtualPath("/Test/Exposure/Lit"
        + std::to_string(static_cast<int>(domain)) + "-"
        + std::to_string(++material_sequence) + ".omat"),
      desc, std::vector<data::ShaderReference> {}));
}

auto ExposureLightingGpuTest::RenderSurface(
  // This fixture call consistently orders rendering path, EV, then frame count.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  bool forward, float ev, unsigned frames) -> void
{
  auto timing = engine::ModuleTimingData {};
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double> {
        frame_delta_seconds,
      }),
  };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  probe->color.reset();
  probe->draws = 0;
  settings.manual_ev = ev;
  scene->GetEnvironment()
    ->TryGetSystem<scene::environment::PostProcessVolume>()
    ->SetExposureSettings(settings);
  scene->Update();
  scene->SyncObservers();
  for (unsigned warmup = 0; warmup < frames; ++warmup) {
    const auto slot = frame::Slot {
      sequence % 3,
    };
    Backend().BeginFrame(
      frame::SequenceNumber {
        sequence + 1,
      },
      slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
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
      .delta_time_seconds = frame_delta_seconds, });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get(), }, });
    facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
      "Lighting", ViewId { surface_view_id, }, view, camera)
        .SetViewStateHandle(persistent_surface_state
            ? CompositionView::ViewStateHandle { surface_view_id, }
            : CompositionView::kInvalidViewStateHandle)
        .SetExposureSourceViewId(surface_source_id)
        .SetExposureOverride(surface_exposure_override));
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { framebuffer.get(), }, });
    facade.SetPipeline(forward ? Renderer::OffscreenPipelineInput::Forward()
                               : Renderer::OffscreenPipelineInput::Deferred());
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
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
  ASSERT_EQ(probe->draws, expected_draws);
  ASSERT_NE(probe->color, nullptr);
  ASSERT_NE(probe->exposure, nullptr);
  const auto domain_data = Read<FrameExposureData>(
    *probe->exposure->buffer, ResourceStates::kShaderResource);
  if (verify_manual_p && settings.mode == engine::ExposureMode::kManual) {
    EXPECT_EQ(domain_data.pre_exposure, std::exp2(-double(ev)));
  }
}

auto ExposureLightingGpuTest::RenderPublishedSurface(bool forward) -> void
{
  scene->Update();
  scene->SyncObservers();
  auto timing = engine::ModuleTimingData {};
  timing.game_delta_time = time::CanonicalDuration {
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double> {
        frame_delta_seconds,
      }),
  };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  frame.SetScene(observer_ptr {
    scene.get(),
  });
  const auto slot = frame::Slot {
    sequence % 3U,
  };
  Backend().BeginFrame(
    frame::SequenceNumber {
      ++sequence,
    },
    slot);
  frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    frame::SequenceNumber {
      sequence,
    },
    engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(observer_ptr {
    &frame,
  });
  auto input = CompositionView::ForScene(
    ViewId {
      surface_view_id,
    },
    view, camera);
  input.view_state_handle = CompositionView::ViewStateHandle {
    surface_view_id,
  };
  input.exposure_source_view_id = surface_source_id;
  input.render_settings.exposure = settings;
  ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = input,
                .render_target = observer_ptr { framebuffer.get(), },
                .composite_source = observer_ptr { framebuffer.get(), }, },
              forward ? ShadingMode::kForward : ShadingMode::kDeferred),
    kInvalidViewId);
  auto loop = co::testing::TestEventLoop {};
  // co::Run completes synchronously before this closure and its captured
  // fixture state leave scope.
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
  co::Run(loop, [&]() -> co::Co<void> {
    co_await renderer_->OnPreRender(observer_ptr {
      &frame,
    });
    co_await renderer_->OnRender(observer_ptr {
      &frame,
    });
  });
  renderer_->OnFrameEnd(observer_ptr {
    &frame,
  });
  Backend().EndFrame(
    frame::SequenceNumber {
      sequence,
    },
    slot);
  WaitForQueueIdle();
  ASSERT_EQ(probe->draws, expected_draws);
  ASSERT_NE(probe->exposure, nullptr);
}

auto ExposureLightingGpuTest::TearDown() -> void
{
  if (probe) {
    probe->prepare = {};
    probe->inspect = {};
    probe->color.reset();
    probe->exposure.reset();
  }
  probe.reset();
  framebuffer.reset();
  ExposureGpuTest::TearDown();
}

} // namespace oxygen::vortex::testing::exposure
