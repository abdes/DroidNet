//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereView.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/Environment/Passes/SkyPass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  constexpr auto kExtent = 3U;
  constexpr auto kCenterPixel = 4U;
  constexpr auto kPixelTolerance = 2.0e-5F;

  class AtmosphereCompositionGpuTest : public ExposureGpuTest {
  protected:
    auto SetUp() -> void override
    {
      ExposureGpuTest::SetUp();
      if (HasFatalFailure()) {
        return;
      }
      pass_.reset();
      renderer_->OnShutdown();
      auto config = RendererConfig {};
      config.upload_queue_key = QueueKeyFor().get();
      renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
        kPhase1DefaultRuntimeCapabilityFamilies
          | RendererCapabilityFamily::kEnvironmentLighting);
      scene_ = std::make_unique<scene::Scene>("Atmosphere composition", 1U);
      scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
      scene_->GetEnvironment()
        ->AddSystem<scene::environment::SkyAtmosphere>()
        .SetEnabled(true);
      auto& authored_fog
        = scene_->GetEnvironment()->AddSystem<scene::environment::Fog>();
      authored_fog.SetEnabled(true);
      authored_fog.SetEnableHeightFog(true);
      authored_fog.SetEnableVolumetricFog(false);
      ctx_.scene = observer_ptr { scene_.get() };
      ctx_.current_view.with_atmosphere = true;
      ctx_.current_view.with_height_fog = true;
      textures_ = std::make_unique<SceneTextures>(Backend(),
        SceneTexturesConfig {
          .extent = { kExtent, kExtent },
          .enable_velocity = false,
          .scene_color_format = Format::kRGBA32Float,
        });
      ap_ = std::make_unique<environment::AtmosphereComposePass>(*renderer_);
      sky_ = std::make_unique<environment::SkyPass>(*renderer_);
      fog_ = std::make_unique<environment::FogPass>(*renderer_);
      depth_slot_ = RegisterTextureView(textures_->GetSceneDepth());
    }

    auto TearDown() -> void override
    {
      WaitForQueueIdle();
      ctx_.scene.reset();
      ctx_.view_constants.reset();
      fog_.reset();
      sky_.reset();
      ap_.reset();
      textures_.reset();
      scene_.reset();
      ExposureGpuTest::TearDown();
    }

    auto RegisterTextureView(const graphics::Texture& texture)
      -> ShaderVisibleIndex
    {
      auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
      auto handle = allocator.AllocateBindless(
        bindless::generated::kTexturesDomain, ResourceViewType::kTexture_SRV);
      const auto slot = allocator.GetShaderVisibleIndex(handle);
      Backend().GetResourceRegistry().RegisterView(texture, std::move(handle),
        graphics::TextureViewDescription {
          .view_type = ResourceViewType::kTexture_SRV,
          .format = texture.GetDescriptor().format,
          .dimension = texture.GetDescriptor().texture_type,
        });
      return slot;
    }

    auto MakeVolume(std::span<const Pixel> slices) -> ExposureSignal
    {
      CHECK_F(!slices.empty());
      constexpr auto kRowPitch = std::size_t { 256U };
      auto volume = CreateRegisteredTexture({
        .width = 1U,
        .height = 1U,
        .depth = static_cast<std::uint32_t>(slices.size()),
        .format = Format::kRGBA32Float,
        .texture_type = TextureType::kTexture3D,
        .is_shader_resource = true,
        .initial_state = ResourceStates::kCommon,
      });
      auto bytes = std::vector<std::byte>(kRowPitch * slices.size());
      for (std::size_t index = 0; index < slices.size(); ++index) {
        auto destination
          = std::span(bytes).subspan(index * kRowPitch, sizeof(Pixel));
        std::memcpy(destination.data(),
          slices.subspan(index, 1U).front().data(), sizeof(Pixel));
      }
      auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
      upload->Update(bytes.data(), bytes.size(), 0U);
      {
        auto recorder = AcquireRecorder("Atmosphere fixture volume upload");
        EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
        EnsureTracked(*recorder, volume, ResourceStates::kCommon);
        recorder->RequireResourceState(*volume, ResourceStates::kCopyDest);
        recorder->FlushBarriers();
        recorder->CopyBufferToTexture(*upload, {
          .buffer_row_pitch = kRowPitch,
          .buffer_slice_pitch = kRowPitch,
          .dst_slice = { .width = 1U, .height = 1U,
            .depth = static_cast<std::uint32_t>(slices.size()), },
        }, *volume);
        recorder->RequireResourceStateFinal(
          *volume, ResourceStates::kShaderResource);
      }
      return { .texture = volume, .srv = RegisterTextureView(*volume) };
    }

    auto BindEnvironment(const EnvironmentStaticData& environment,
      const EnvironmentViewData& environment_view, ViewConstants::GpuData view,
      float pre_exposure = 1.0F) -> void
    {
      auto scene_textures = SceneTextureBindings {};
      scene_textures.scene_depth_srv = depth_slot_.get();
      auto environment_bindings = EnvironmentFrameBindings {};
      environment_bindings.environment_static_slot
        = PublishFixtureData(environment);
      environment_bindings.environment_view_slot
        = PublishFixtureData(environment_view);
      auto view_bindings = ViewFrameBindings {};
      view_bindings.scene_texture_frame_slot
        = PublishFixtureData(scene_textures);
      view_bindings.environment_frame_slot
        = PublishFixtureData(environment_bindings);
      view_bindings.frame_exposure_slot = PublishFixtureData(FrameExposureData {
        .pre_exposure = pre_exposure,
        .one_over_pre_exposure = 1.0F / pre_exposure,
      });
      view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
        PublishFixtureData(view_bindings),
      };
      auto constants = CreateUploadBuffer(
        SizeBytes { 256U }, graphics::BufferUsage::kConstant);
      constants->Update(&view, sizeof(view), 0U);
      ctx_.view_constants = constants;
    }

    //! Writes the case's color and depth by copy: clears could only use the
    //! scene textures' creation clear values without slow-clear hints.
    auto Clear(const Pixel& color, float depth) -> void
    {
      auto& scene_color = textures_->GetSceneColor();
      auto& scene_depth = textures_->GetSceneDepth();
      FillColor(scene_color, color, ResourceStates::kRenderTarget);
      const auto& desc = scene_depth.GetDescriptor();
      const std::vector<float> depths(
        static_cast<std::size_t>(desc.width) * desc.height, depth);
      UploadDepth(scene_depth, depths, ResourceStates::kDepthWrite);
    }

    auto DrawAerial() -> std::vector<Pixel>
    {
      const auto state = SubmitCommands("Atmosphere composition regression",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return ap_->Record(ctx_, recorder, *textures_);
        });
      EXPECT_TRUE(state.executed);
      return ReadFloatTexture(textures_->GetSceneColor());
    }

    auto DrawSky() -> std::vector<Pixel>
    {
      const auto state = SubmitCommands("Sky holdout regression",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return sky_->Record(ctx_, recorder, *textures_);
        });
      EXPECT_TRUE(state.executed);
      return ReadFloatTexture(textures_->GetSceneColor());
    }

    auto DrawFog() -> std::vector<Pixel>
    {
      const auto state = SubmitCommands("Fog holdout regression",
        [&](graphics::CommandRecorder& recorder) -> auto {
          return fog_->Record(ctx_, recorder, *textures_);
        });
      EXPECT_TRUE(state.executed);
      return ReadFloatTexture(textures_->GetSceneColor());
    }

  private:
    std::unique_ptr<scene::Scene> scene_;
    std::unique_ptr<SceneTextures> textures_;
    std::unique_ptr<environment::AtmosphereComposePass> ap_;
    std::unique_ptr<environment::SkyPass> sky_;
    std::unique_ptr<environment::FogPass> fog_;
    ShaderVisibleIndex depth_slot_ { kInvalidShaderVisibleIndex };
  };
} // namespace

NOLINT_TEST_F(AtmosphereCompositionGpuTest, AerialDepthUsesThePerPixelRayOrigin)
{
  constexpr auto kVolumeDepth = 16U;
  constexpr auto kVolumeDistanceKm = 2.0F;
  constexpr auto kPlaneDistance = 100.0F;
  constexpr auto kFarPlane = 2048.0F;
  constexpr auto kOrthoHalfExtent = 1500.0F;
  constexpr auto kSamplingTolerance = 5.0e-4F;
  auto slices = std::array<Pixel, kVolumeDepth> {};
  for (std::size_t index = 0; index < slices.size(); ++index) {
    const auto value = (static_cast<float>(index) + 0.5F) / kVolumeDepth;
    slices.at(index) = { value, value, value, 1.0F };
  }
  const auto volume = MakeVolume(slices);
  auto environment = EnvironmentStaticData {};
  environment.atmosphere.camera_volume_lut_slot = volume.srv.get();
  auto environment_view = EnvironmentViewData {};
  environment_view.camera_aerial_volume_depth_params = {
    kVolumeDepth,
    1.0F / kVolumeDepth,
    kVolumeDistanceKm / kVolumeDepth,
    kVolumeDepth / kVolumeDistanceKm,
  };
  environment_view.sky_aerial_luminance_aerial_start_depth_km.w = 0.0F;
  for (const bool orthographic : { true, false }) {
    for (const bool reverse_depth : { false, true }) {
      for (const bool transformed : { false, true }) {
        for (const float near_plane : { 1.0F, -10.0F }) {
          if (!orthographic && near_plane < 0.0F) {
            continue;
          }
          SCOPED_TRACE(::testing::Message()
            << "ortho=" << orthographic << " reverse=" << reverse_depth
            << " transformed=" << transformed << " near=" << near_plane);
          auto view = ViewConstants::GpuData {};
          view.camera_position = transformed
            ? glm::vec3 { 1234.0F, -2345.0F, 567.0F }
            : glm::vec3 { 0.0F };
          const auto forward = transformed
            ? glm::normalize(glm::vec3 { 0.3F, 0.8F, -0.5F })
            : glm::vec3 { 0.0F, 0.0F, -1.0F };
          const auto up = transformed ? glm::vec3 { 0.0F, 0.0F, 1.0F }
                                      : glm::vec3 { 0.0F, 1.0F, 0.0F };
          view.view_matrix = glm::lookAtRH(
            view.camera_position, view.camera_position + forward, up);
          const auto first_depth = reverse_depth ? kFarPlane : near_plane;
          const auto last_depth = reverse_depth ? near_plane : kFarPlane;
          view.projection_matrix = orthographic
            ? glm::orthoRH_ZO(-kOrthoHalfExtent, kOrthoHalfExtent,
                -kOrthoHalfExtent, kOrthoHalfExtent, first_depth, last_depth)
            : glm::perspectiveRH_ZO(std::numbers::pi_v<float> / 2.0F, 1.0F,
                first_depth, last_depth);
          view.inverse_view_projection_matrix
            = glm::inverse(view.projection_matrix * view.view_matrix);
          view.reverse_z = reverse_depth ? 1U : 0U;
          view.is_orthographic = orthographic ? 1U : 0U;
          const auto clip = view.projection_matrix
            * glm::vec4 { 0.0F, 0.0F, -kPlaneDistance, 1.0F };
          BindEnvironment(environment, environment_view, view);
          Clear(Pixel {}, clip.z / clip.w);
          const auto pixels = DrawAerial();
          ASSERT_EQ(pixels.size(), kExtent * kExtent);
          for (std::size_t y = 0U; y < kExtent; ++y) {
            for (std::size_t x = 0U; x < kExtent; ++x) {
              const auto ndc_x
                = (2.0F * (static_cast<float>(x) + 0.5F) / kExtent) - 1.0F;
              const auto ndc_y
                = (2.0F * (static_cast<float>(y) + 0.5F) / kExtent) - 1.0F;
              const auto distance = orthographic ? kPlaneDistance - near_plane
                                                 : kPlaneDistance
                  * std::sqrt(1.0F + (ndc_x * ndc_x) + (ndc_y * ndc_y));
              const auto expected
                = std::sqrt(distance / (1000.0F * kVolumeDistanceKm));
              const auto& pixel = pixels.at((y * kExtent) + x);
              EXPECT_NEAR(pixel.at(0), expected, kSamplingTolerance);
              EXPECT_NEAR(pixel.at(1), expected, kSamplingTolerance);
              EXPECT_NEAR(pixel.at(2), expected, kSamplingTolerance);
            }
          }
        }
      }
    }
  }
}

NOLINT_TEST_F(
  AtmosphereCompositionGpuTest, ZeroAerialStrengthPreservesExtinction)
{
  constexpr Pixel kVolume { 0.1F, 0.2F, 0.3F, 0.4F };
  constexpr Pixel kBackground { 0.5F, 0.25F, 0.75F, 0.5F };
  const auto volume = MakeVolume(std::array<Pixel, 1> { kVolume });
  auto environment = EnvironmentStaticData {};
  environment.atmosphere.camera_volume_lut_slot = volume.srv.get();
  auto environment_view = EnvironmentViewData {};
  environment_view.sky_aerial_luminance_aerial_start_depth_km.w = 0.0F;
  environment_view.camera_aerial_volume_depth_params
    = { 1.0F, 1.0F, 1.0e-4F, 10000.0F };
  auto view = ViewConstants::GpuData {};
  view.reverse_z = 0U;
  for (const float strength : { 0.0F, 0.00005F, 0.0001F, 0.0002F, 1.0F }) {
    SCOPED_TRACE(strength);
    environment_view.aerial_scattering_strength = strength;
    BindEnvironment(environment, environment_view, view);
    Clear(kBackground, 0.5F);
    const auto capture = strength == 0.0F
      ? BeginOptionalCapture()
      : observer_ptr<graphics::FrameCaptureController> {};
    const auto pixels = DrawAerial();
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    ASSERT_EQ(pixels.size(), kExtent * kExtent);
    for (const auto& pixel : pixels) {
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel.at(channel),
          (kBackground.at(channel) * kVolume.at(3))
            + (kVolume.at(channel) * strength),
          kPixelTolerance);
      }
      EXPECT_NEAR(pixel.at(3),
        (1.0F - kVolume.at(3)) + (kBackground.at(3) * kVolume.at(3)),
        kPixelTolerance);
    }
  }
}

NOLINT_TEST_F(AtmosphereCompositionGpuTest,
  SkyHoldoutPreservesIndependentFogAndBackgroundCoverage)
{
  constexpr Pixel kSky { 0.125F, 0.25F, 0.5F, 0.4F };
  constexpr Pixel kBackground { 0.25F, 0.5F, 0.75F, 0.25F };
  constexpr auto kFogColor = std::array { 0.2F, 0.4F, 0.6F };
  constexpr auto kDiskColor = std::array { 0.5F, 0.25F, 0.125F };
  const auto transmittance
    = MakeSignal(1U, 1U, std::array<Pixel, 1> { Pixel { 1, 1, 1, 1 } }, 1U,
      Format::kRGBA32Float, true);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  auto environment_view = environment::internal::BuildAtmosphereViewData(
    stable, {}, {}, true, false);
  // A wide disk fully covers the center pixel despite the 3x3 derivative quad.
  environment_view.atmosphere_light0_direction_angular_size
    = { 0.0F, 0.0F, 1.0F, 1.0F };
  environment_view.atmosphere_light0_disk_luminance_rgb = {
    kDiskColor.at(0),
    kDiskColor.at(1),
    kDiskColor.at(2),
    1.0F,
  };
  auto view = ViewConstants::GpuData {};
  view.reverse_z = 0U;
  for (const float pre_exposure : { 1.0F, 4.0F }) {
    const auto source = MakeSignal(1U, 1U,
      std::array<Pixel, 1> {
        Pixel {
          kSky.at(0) * pre_exposure,
          kSky.at(1) * pre_exposure,
          kSky.at(2) * pre_exposure,
          kSky.at(3),
        },
      },
      1U, Format::kRGBA32Float, true);
    for (const bool holdout : { false, true }) {
      for (const bool disk : { false, true }) {
        for (const bool fog_enabled : { false, true }) {
          for (const bool fog_holdout : { false, true }) {
            SCOPED_TRACE(::testing::Message()
              << "holdout=" << holdout << " disk=" << disk
              << " fog=" << fog_enabled << " fog holdout=" << fog_holdout
              << " P=" << pre_exposure);
            auto environment = EnvironmentStaticData {};
            environment.atmosphere.enabled = 1U;
            environment.atmosphere.sky_view_lut_slot = source.srv.get();
            environment.atmosphere.sky_view_lut_width = 1U;
            environment.atmosphere.sky_view_lut_height = 1U;
            environment.atmosphere.transmittance_lut_slot
              = transmittance.srv.get();
            environment.atmosphere.transmittance_lut_width = 1U;
            environment.atmosphere.transmittance_lut_height = 1U;
            environment.atmosphere.sun_disk_enabled = disk ? 1U : 0U;
            environment.fog.flags = fog_enabled ? kGpuFogFlagEnabled
                | kGpuFogFlagHeightFogEnabled | kGpuFogFlagRenderInMainPass
                | (fog_holdout ? kGpuFogFlagHoldout : 0U)
                                                : 0U;
            environment.fog.primary_density = 0.01F;
            environment.fog.max_opacity = 0.5F;
            environment.fog.min_transmittance = 0.5F;
            environment.fog.fog_inscattering_luminance_rgb = kFogColor;
            environment_view
              .trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass
              .z = holdout ? 1.0F : 0.0F;
            BindEnvironment(environment, environment_view, view, pre_exposure);
            Clear(
              Pixel {
                kBackground.at(0) * pre_exposure,
                kBackground.at(1) * pre_exposure,
                kBackground.at(2) * pre_exposure,
                kBackground.at(3),
              },
              1.0F);
            const auto pixels = DrawSky();
            ASSERT_EQ(pixels.size(), kExtent * kExtent);
            const auto& pixel = pixels.at(kCenterPixel);
            const auto fog_transmittance = fog_enabled ? 0.5F : 1.0F;
            const auto remaining
              = holdout ? kSky.at(3) * fog_transmittance : 0.0F;
            for (std::size_t channel = 0; channel < 3U; ++channel) {
              const auto sky = holdout
                ? 0.0F
                : (kSky.at(channel) + (disk ? kDiskColor.at(channel) : 0.0F))
                  * fog_transmittance;
              const auto fog = fog_enabled && !fog_holdout
                ? kFogColor.at(channel) * 0.5F
                : 0.0F;
              const auto expected
                = (sky + fog + (kBackground.at(channel) * remaining))
                * pre_exposure;
              EXPECT_NEAR(pixel.at(channel), expected, kPixelTolerance);
            }
            EXPECT_NEAR(pixel.at(3),
              1.0F - remaining + (kBackground.at(3) * remaining),
              kPixelTolerance);
          }
        }
      }
    }
  }
}

NOLINT_TEST_F(AtmosphereCompositionGpuTest, FogHoldoutKeepsGeometryExtinction)
{
  constexpr Pixel kBackground { 0.5F, 0.25F, 0.75F, 1.0F };
  auto environment = EnvironmentStaticData {};
  environment.fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
    | kGpuFogFlagRenderInMainPass;
  environment.fog.primary_density = 1.0F;
  environment.fog.fog_inscattering_luminance_rgb = { 0.2F, 0.4F, 0.6F };
  auto environment_view = EnvironmentViewData {};
  environment_view.flags |= kEnvironmentViewFlagHeightFog;
  auto view = ViewConstants::GpuData {};
  view.reverse_z = 0U;
  BindEnvironment(environment, environment_view, view);
  Clear(kBackground, 0.5F);
  const auto normal = DrawFog();
  environment.fog.flags |= kGpuFogFlagHoldout;
  BindEnvironment(environment, environment_view, view);
  Clear(kBackground, 0.5F);
  const auto held = DrawFog();
  ASSERT_EQ(held.size(), kExtent * kExtent);
  ASSERT_EQ(normal.size(), held.size());
  // Height fog uses a base-two density profile and base-two extinction.
  const auto transmission = std::exp2(-0.5F * std::numbers::ln2_v<float>);
  for (std::size_t channel = 0; channel < 3U; ++channel) {
    EXPECT_NEAR(held.at(kCenterPixel).at(channel),
      kBackground.at(channel) * transmission, kPixelTolerance);
    EXPECT_GT(
      normal.at(kCenterPixel).at(channel), held.at(kCenterPixel).at(channel));
  }
  EXPECT_FLOAT_EQ(held.at(kCenterPixel).at(3), normal.at(kCenterPixel).at(3));
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, AerialExtinctionSurvivesZeroStrengthAcrossPaths)
{
  constexpr auto kSurfaceDistance = 1000.0F;
  constexpr auto kCameraAltitude = 2000.0F;
  constexpr auto kEmission = 2.0F;
  ASSERT_TRUE(
    camera.GetTransform().SetLocalPosition({ 0, 0, kCameraAltitude }));
  ASSERT_TRUE(
    mesh_node.GetTransform().SetLocalPosition({ 0, 0, kCameraAltitude }));
  ASSERT_TRUE(
    mesh_node.GetTransform().SetLocalScale(glm::vec3 { kSurfaceDistance }));
  auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
  ASSERT_TRUE(lens);
  lens->get().SetFarPlane(10000.0F);
  auto& atmosphere
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetRayleighScatteringRgb(glm::vec3 { 0.0F });
  atmosphere.SetMieScatteringRgb(glm::vec3 { 0.0F });
  atmosphere.SetOzoneAbsorptionRgb(glm::vec3 { 0.0F });
  atmosphere.SetMieAbsorptionRgb(glm::vec3 { 0.0005F });
  atmosphere.SetMieScaleHeightMeters(1.0e6F);
  atmosphere.SetAerialPerspectiveStartDepthMeters(0.0F);
  auto prepare = std::move(probe->prepare);
  probe->prepare
    = [prepare = std::move(prepare)](RenderContext& context) -> void {
    prepare(context);
    context.current_view.with_atmosphere = true;
  };
  for (const bool forward : { false, true }) {
    for (const auto domain : {
           data::MaterialDomain::kOpaque,
           data::MaterialDomain::kMasked,
           data::MaterialDomain::kAlphaBlended,
         }) {
      SCOPED_TRACE(::testing::Message() << "forward=" << forward << " domain="
                                        << static_cast<unsigned>(domain));
      SetSurface(domain, kEmission);
      Pixel reference {};
      for (const float strength : { 1.0F, 0.0F, 0.00005F, 0.0001F, 0.0002F }) {
        atmosphere.SetAerialScatteringStrength(strength);
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
        ASSERT_NE(probe->color, nullptr);
        const auto pixel = ReadFloatTexture(*probe->color).front();
        if (strength == 1.0F) {
          reference = pixel;
          const auto coverage
            = domain == data::MaterialDomain::kAlphaBlended ? 0.5F : 1.0F;
          // The real volume integrates a nearly homogeneous 1 km absorbing
          // path.
          EXPECT_NEAR(
            pixel.at(0) / (kEmission * coverage), std::exp(-0.5F), 0.015F);
        } else {
          for (std::size_t channel = 0; channel < 4U; ++channel) {
            EXPECT_NEAR(
              pixel.at(channel), reference.at(channel), kPixelTolerance);
          }
        }
      }
      atmosphere.SetHoldout(true);
      atmosphere.SetAerialScatteringStrength(1.0F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
      const auto held = ReadFloatTexture(*probe->color).front();
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        EXPECT_NEAR(held.at(channel), reference.at(channel), kPixelTolerance);
      }
      // A translucent surface also exposes the held sky's fractional coverage.
      EXPECT_GE(held.at(3), 0.0F);
      EXPECT_LE(held.at(3), reference.at(3));
      atmosphere.SetHoldout(false);
    }
  }

  // Isolate surface AP from the background behind translucent material.
  atmosphere.SetSkyLuminanceFactorRgb(glm::vec3 { 0.0F });
  atmosphere.SetSunDiskEnabled(false);
  atmosphere.SetRayleighScatteringRgb(glm::vec3 { 0.000005F });
  atmosphere.SetMieAbsorptionRgb(glm::vec3 { 0.0F });
  auto sun = scene->CreateNode("Aerial holdout sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(sun.AttachLight(std::move(light)));
  for (const bool forward : { false, true }) {
    for (const auto domain : {
           data::MaterialDomain::kOpaque,
           data::MaterialDomain::kAlphaBlended,
         }) {
      SetSurface(domain, kEmission);
      atmosphere.SetAerialScatteringStrength(0.0F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
      const auto extinction_only = ReadFloatTexture(*probe->color).front();
      atmosphere.SetAerialScatteringStrength(1.0F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
      const auto scattering = ReadFloatTexture(*probe->color).front();
      atmosphere.SetHoldout(true);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
      const auto held = ReadFloatTexture(*probe->color).front();
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        EXPECT_GT(scattering.at(channel), extinction_only.at(channel));
        EXPECT_NEAR(
          held.at(channel), extinction_only.at(channel), kPixelTolerance);
      }
      atmosphere.SetHoldout(false);
    }
  }
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, CapturedIblIgnoresAtmosphereAndFogHoldout)
{
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyLight>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkyLightSource::kCapturedScene);
  sky.SetSpecularIntensity(0.0F);
  auto& atmosphere
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(false);
  fog.SetFogDensity(0.001F);
  fog.SetMaxOpacity(0.5F);
  fog.SetFogInscatteringLuminance({ 0.2F, 0.4F, 0.6F });
  fog.SetVisibleInRealTimeSkyCaptures(true);
  auto sun = scene->CreateNode("Holdout capture sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(sun.AttachLight(std::move(light)));
  // Isolate lighting received from the capture from visible-view compositing.
  auto prepare = std::move(probe->prepare);
  probe->prepare
    = [prepare = std::move(prepare)](RenderContext& context) -> void {
    prepare(context);
    context.current_view.with_atmosphere = false;
    context.current_view.with_height_fog = false;
  };
  SetSurface(data::MaterialDomain::kOpaque);
  for (const auto fog_color :
    { glm::vec3 { 0.2F, 0.4F, 0.6F }, glm::vec3 { 0.6F, 0.3F, 0.1F } }) {
    // A changed capture source also exercises regeneration while holdout is on.
    fog.SetFogInscatteringLuminance(fog_color);
    atmosphere.SetHoldout(true);
    fog.SetHoldout(true);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto held = ReadFloatTexture(*probe->color).front();
    const auto held_state = renderer_->InspectSkyLight(*scene);
    ASSERT_TRUE(held_state.usable);
    sky.SetDiffuseIntensity(0.0F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto unlit = ReadFloatTexture(*probe->color).front();
    sky.SetDiffuseIntensity(1.0F);
    atmosphere.SetHoldout(false);
    fog.SetHoldout(false);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto normal = ReadFloatTexture(*probe->color).front();
    EXPECT_EQ(renderer_->InspectSkyLight(*scene).published_revision,
      held_state.published_revision);
    // Force fresh non-held products for the same physical source, so equality
    // cannot pass merely by reusing an incorrectly darkened held capture.
    fog.SetFogInscatteringLuminance(fog_color * 2.0F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    fog.SetFogInscatteringLuminance(fog_color);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F));
    const auto regenerated = ReadFloatTexture(*probe->color).front();
    EXPECT_GT(renderer_->InspectSkyLight(*scene).published_revision,
      held_state.published_revision);
    for (std::size_t channel = 0; channel < 3U; ++channel) {
      EXPECT_GT(held.at(channel), unlit.at(channel));
      EXPECT_NEAR(held.at(channel), normal.at(channel), kPixelTolerance);
      EXPECT_NEAR(held.at(channel), regenerated.at(channel), kPixelTolerance);
    }
  }
}

} // namespace oxygen::vortex::testing::exposure
