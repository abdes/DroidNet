//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
// MSVC declares the platform-specific _dupenv_s in the C runtime header.
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <stdlib.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/test/CommandRecordingTestSupport.h>
#include <Oxygen/Graphics/Common/test/HeapAllocationFailure.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/Diagnostics/GpuWorkSummary.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblCaptureLease.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Environment/Types/SkyLightEnvironmentModel.h>
#include <Oxygen/Vortex/Environment/Types/StaticSkyLightProducts.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Types/Generated.ExposureContracts.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>
#include <Oxygen/Vortex/ViewExtension.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/test/Support/FirstSurface.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  using data::MaterialDomain;
  using environment::IblCaptureError;
  using environment::IblCaptureLease;
  using environment::IblProductMetadata;
  using environment::internal::IblGpuProducts;
  using graphics::ResourceStates;
  using scene::environment::Fog;
  using scene::environment::SkyLight;
  using scene::environment::SkyLightSource;
  using scene::environment::SkySphere;
  using vortex::testing::CubeShape;
  using vortex::testing::FirstSurfaceOf;
  using vortex::testing::RendererPublicationProbe;
  using vortex::testing::SyntheticTexel;

  using CaptureResult = Result<IblCaptureLease, IblCaptureError>;

  constexpr std::array kAllDomains {
    MaterialDomain::kOpaque,
    MaterialDomain::kMasked,
    MaterialDomain::kAlphaBlended,
  };
  constexpr std::array kOpaqueAndBlended {
    MaterialDomain::kOpaque,
    MaterialDomain::kAlphaBlended,
  };

  //! Alpha-blended test materials cover half of the surface.
  auto Coverage(const MaterialDomain domain) -> double
  {
    return domain == MaterialDomain::kAlphaBlended ? 0.5 : 1.0;
  }

  struct CubeTexel {
    std::uint32_t face;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t size;
  };

  // Independent face basis; hardware cube Y is Oxygen world Z.
  auto WorldDirection(const CubeTexel texel) -> glm::vec3
  {
    const auto size = static_cast<float>(texel.size);
    const auto u = (2.0F * (static_cast<float>(texel.x) + 0.5F) / size) - 1.0F;
    const auto v = (2.0F * (static_cast<float>(texel.y) + 0.5F) / size) - 1.0F;
    const std::array directions {
      glm::vec3 { 1, -v, -u },
      glm::vec3 { -1, -v, u },
      glm::vec3 { u, 1, v },
      glm::vec3 { u, -1, -v },
      glm::vec3 { u, -v, 1 },
      glm::vec3 { -u, -v, -1 },
    };
    const auto cube = glm::normalize(directions.at(texel.face));
    return { cube.x, -cube.z, cube.y };
  }

  //! Texels of value `value` everywhere.
  auto UniformTexel(const Pixel& value) -> SyntheticTexel
  {
    return [value](std::uint32_t /*layer*/, std::uint32_t /*x*/,
             std::uint32_t /*y*/) -> Pixel { return value; };
  }

  // Radiance that rises linearly with the world-up component of a direction.
  constexpr std::array kGradientBase { 0.75F, 0.5F, 0.25F };
  constexpr std::array kGradientSlope { 0.5F, 0.25F, 0.125F };

  auto VerticalGradient(const std::uint32_t size) -> SyntheticTexel
  {
    return [size](const std::uint32_t face, const std::uint32_t x,
             const std::uint32_t y) -> Pixel {
      const auto up
        = WorldDirection({ .face = face, .x = x, .y = y, .size = size }).z;
      return Pixel {
        kGradientBase[0] + (kGradientSlope[0] * up),
        kGradientBase[1] + (kGradientSlope[1] * up),
        kGradientBase[2] + (kGradientSlope[2] * up),
        1.0F,
      };
    };
  }

  //! The UNORM encoding of the tangent-space normal (0.6, 0, 0.8).
  constexpr Pixel kTiltedNormal { 0.8F, 0.5F, 0.9F, 1.0F };
  constexpr double kTiltedNormalZ = 0.8;

  struct MaterialProperties {
    float roughness;
    float metallic;
  };

  constexpr MaterialProperties kRoughDielectric {
    .roughness = 1.0F,
    .metallic = 0.0F,
  };
  constexpr glm::vec3 kWhite { 1.0F };

  // Native shader uses a clamped linear lookup. NoV=1 selects the last column;
  // interpolate the immutable U16 source texels independently on the CPU.
  auto BrdfAtNormalIncidence(const float roughness) -> std::array<double, 2>
  {
    namespace ibl = environment::internal;
    const auto data = ibl::GetIblBrdfLookup();
    const double y = (roughness * ibl::kIblBrdfHeight) - 0.5;
    const int low = static_cast<int>(std::floor(y));
    const double fraction = y - low;
    std::array<double, 2> result {};
    for (unsigned c = 0; c < 2; ++c) {
      const auto at = [&](const int row) -> double {
        const auto index
          = (std::clamp(row, 0, static_cast<int>(ibl::kIblBrdfHeight) - 1)
              * ibl::kIblBrdfWidth)
          + ibl::kIblBrdfWidth - 1;
        return static_cast<double>(data[index][c]) / 65535.0;
      };
      result[c] = (at(low) * (1.0 - fraction)) + (at(low + 1) * fraction);
    }
    return result;
  }

  //! Sky light controls that scale the IBL contribution without recapture.
  struct SkyControls {
    float intensity { 1.0F };
    float diffuse { 1.0F };
    float specular { 1.0F };
    bool reflections { true };
    glm::vec3 tint { 1.0F };
  };

  //! The surface as the IBL evaluator sees it.
  struct SurfaceInputs {
    glm::vec3 color { kWhite };
    float metallic { 0.0F };
    float specular { 0.5F };
    float roughness { 1.0F };
  };

  //! Split-sum IBL radiance of `channel` at normal incidence, for a uniform
  //! `radiance` and full coverage.
  auto SplitSum(const Pixel& radiance, const SurfaceInputs& surface,
    const SkyControls& sky, const unsigned channel) -> double
  {
    const auto c = static_cast<glm::length_t>(channel);
    const auto brdf = BrdfAtNormalIncidence(surface.roughness);
    const bool metal = surface.metallic > 0.0F;
    const double dielectric_f0 = 0.08 * surface.specular;
    const double f0 = metal ? surface.color[c] : dielectric_f0;
    // F90 keeps the retained 50 * F0.g rule.
    const double f90
      = std::min(1.0, 50.0 * (metal ? surface.color.g : dielectric_f0));
    const double diffuse
      = surface.color[c] * (1.0 - surface.metallic) * sky.diffuse;
    const double specular = sky.reflections
      ? ((f0 * brdf[0]) + (f90 * brdf[1])) * sky.specular
      : 0.0;
    return radiance[channel] * sky.intensity * sky.tint[c]
      * (diffuse + specular);
  }

  auto ExpectUnlit(const Pixel& pixel) -> void
  {
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_FLOAT_EQ(pixel[c], 0.0F);
    }
  }

  //! The G-buffer texels of the surface, as a deferred evaluator reads them.
  struct GBufferTexels {
    Pixel base_color;
    Pixel material; // metallic, specular, roughness
    Pixel normal;
  };

  auto DecodeGBuffer(const GBufferTexels& texels) -> SurfaceInputs
  {
    return {
      .color
      = { texels.base_color[0], texels.base_color[1], texels.base_color[2] },
      .metallic = texels.material[0],
      .specular = texels.material[1],
      .roughness = texels.material[2],
    };
  }

  //! Largest per-channel error of `actual` relative to `reference`.
  auto MaximumRelativeError(const std::vector<Pixel>& actual,
    const std::vector<Pixel>& reference) -> double
  {
    CHECK_EQ_F(actual.size(), reference.size());
    double maximum = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
      for (unsigned c = 0; c < 3; ++c) {
        const auto expected = static_cast<double>(reference[i][c]);
        maximum = std::max(maximum,
          std::abs(static_cast<double>(actual[i][c]) - expected)
            / std::max(1.0e-5, expected));
      }
    }
    return maximum;
  }

  struct CodeDifference {
    double peak;
    double rms;
  };

  //! Difference between two display images, in 8-bit code equivalents.
  auto DisplayDifference(const std::vector<Pixel>& actual,
    const std::vector<Pixel>& reference) -> CodeDifference
  {
    CHECK_EQ_F(actual.size(), reference.size());
    double peak = 0;
    double squared = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
      for (unsigned c = 0; c < 3; ++c) {
        const double codes
          = 255.0 * (static_cast<double>(actual[i][c]) - reference[i][c]);
        squared += codes * codes;
        peak = std::max(peak, std::abs(codes));
      }
    }
    return {
      .peak = peak,
      .rms = std::sqrt(squared / static_cast<double>(actual.size() * 3U)),
    };
  }

  //! Writes images and a manifest when `OXYGEN_IBL_IMAGE` names the manifest.
  class ImageDump {
  public:
    ImageDump()
    {
      char* value = nullptr;
      std::size_t size = 0;
      if (_dupenv_s(&value, &size, "OXYGEN_IBL_IMAGE") != 0) {
        ADD_FAILURE() << "OXYGEN_IBL_IMAGE could not be read";
        return;
      }
      const auto owned
        = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
      if (value != nullptr) {
        manifest_ = value;
        std::filesystem::create_directories(manifest_.parent_path());
      }
    }

    //! Writes `pixels` as raw RGBA32F next to the manifest.
    auto Save(const std::string& name, const std::vector<Pixel>& pixels) const
      -> void
    {
      if (manifest_.empty()) {
        return;
      }
      std::ofstream stream(manifest_.parent_path()
          / (manifest_.stem().string() + "-" + name + ".rgba32f"),
        std::ios::binary);
      ASSERT_TRUE(stream.good());
      const auto bytes = std::as_bytes(std::span { pixels });
      auto chars = std::vector<char>(bytes.size());
      std::ranges::transform(
        bytes, chars.begin(), [](const std::byte value) -> char {
          return std::bit_cast<char>(value);
        });
      stream.write(chars.data(), static_cast<std::streamsize>(chars.size()));
      ASSERT_TRUE(stream.good());
    }

    auto WriteManifest(const nlohmann::json& manifest) const -> void
    {
      if (manifest_.empty()) {
        return;
      }
      std::ofstream stream(manifest_);
      ASSERT_TRUE(stream.good());
      stream << manifest.dump(2);
      ASSERT_TRUE(stream.good());
    }

  private:
    std::filesystem::path manifest_;
  };

  //! A height fog that only real-time sky captures see.
  struct CapturedFog {
    glm::vec3 luminance { 1.0F };
    float density { 0.01F };
    float falloff_per_meter { 0.0F };
    float max_opacity { 1.0F };
  };

  class IblSurfaceGpuTest : public ExposureLightingGpuTest {
  protected:
    //! A retained capture's metadata and the first texel of its cube.
    struct RetainedCapture {
      IblProductMetadata metadata;
      Pixel first_texel;
    };

    //! Linear scene values of the surface rendered without and with sky light.
    struct SkyContribution {
      std::vector<Pixel> baseline;
      std::vector<Pixel> lit;

      [[nodiscard]] auto Difference() const -> std::vector<Pixel>
      {
        CHECK_EQ_F(lit.size(), baseline.size());
        auto result = lit;
        for (std::size_t i = 0; i < result.size(); ++i) {
          for (unsigned c = 0; c < 3; ++c) {
            result[i][c] -= baseline[i][c];
          }
        }
        return result;
      }
    };

    auto SetUp() -> void override
    {
      ExposureLightingGpuTest::SetUp();
      auto& sky = scene->GetEnvironment()->AddSystem<SkyLight>();
      sky.SetEnabled(true);
      sky.SetSource(SkyLightSource::kSpecifiedCubemap);
      sky.SetLowerHemisphereIsSolidColor(false);
    }

    auto TearDown() -> void override
    {
      if (probe) {
        probe->after_render = {};
        probe->inspect = {};
      }
      packed_input_textures_.fill(nullptr);
      published_.reset();
      admitted_ = Err(IblCaptureError::kUnavailable);
      ExposureLightingGpuTest::TearDown();
    }

    auto Sky() -> SkyLight&
    {
      return *scene->GetEnvironment()->TryGetSystem<SkyLight>();
    }

    auto SkyState() const -> SkyLightRuntimeState
    {
      return renderer_->InspectSkyLight(*scene);
    }

    auto Diagnostics() -> DiagnosticsService&
    {
      return renderer_->GetDiagnosticsService();
    }

    auto SceneRendererOf() -> SceneRenderer*
    {
      return RendererPublicationProbe::GetSceneRenderer(*renderer_);
    }

    auto IblOwner() -> environment::internal::IblProcessor&
    {
      return RendererPublicationProbe::IblOwner(*SceneRendererOf());
    }

    //! Whether the latest diagnostics frame reports `code`, of `severity`
    //! when one is given.
    auto HasIssue(const std::string_view code,
      const std::optional<DiagnosticsSeverity> severity = {}) -> bool
    {
      const auto snapshot = Diagnostics().GetLatestSnapshot();
      return std::ranges::any_of(
        snapshot.issues, [&](const DiagnosticsIssue& issue) -> bool {
          return issue.code == code
            && (!severity.has_value() || issue.severity == *severity);
        });
    }

    auto SurfaceColor() -> Pixel
    {
      return ReadFloatTexture(*probe->color).at(0);
    }

    auto RenderDeferred(const unsigned frames = 5U) -> void
    {
      RenderSurface(false, 0.0F, frames);
    }

    auto ApplySky(const SkyControls& controls) -> void
    {
      Sky().SetIntensityMul(controls.intensity);
      Sky().SetDiffuseIntensity(controls.diffuse);
      Sky().SetSpecularIntensity(controls.specular);
      Sky().SetAffectReflections(controls.reflections);
      Sky().SetTintRgb(controls.tint);
    }

    auto AddCube(const std::uint32_t size, const SyntheticTexel& texel,
      const Format format = Format::kRGBA32Float) -> content::ResourceKey
    {
      return owned_asset_loader_->AddSyntheticTexture(
        CubeShape(size, format), texel);
    }

    auto AddUniformCube(const Pixel& value,
      const Format format = Format::kRGBA32Float) -> content::ResourceKey
    {
      return AddCube(16U, UniformTexel(value), format);
    }

    auto AddTexel(const Pixel& value) -> content::ResourceKey
    {
      return owned_asset_loader_->AddSyntheticTexture({}, UniformTexel(value));
    }

    auto Material(const MaterialDomain domain,
      const MaterialProperties properties, const glm::vec3& color = kWhite,
      const content::ResourceKey normal = {}) -> void
    {
      auto desc = data::pak::render::MaterialAssetDesc {};
      desc.material_domain = static_cast<std::uint8_t>(domain);
      desc.flags = data::pak::render::kMaterialFlag_DoubleSided;
      if (normal.get() == 0U) {
        desc.flags |= data::pak::render::kMaterialFlag_NoTextureSampling;
      }
      if (domain == MaterialDomain::kMasked) {
        desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
      }
      for (unsigned c = 0; c < 3; ++c) {
        desc.base_color[c] = color[static_cast<glm::length_t>(c)];
      }
      desc.base_color[3]
        = domain == MaterialDomain::kAlphaBlended ? 0.5F : 1.0F;
      desc.roughness = data::Unorm16 { properties.roughness };
      desc.metalness = data::Unorm16 { properties.metallic };
      desc.ambient_occlusion = data::Unorm16 { 1.0F };
      desc.normal_scale = 1.0F;
      desc.uv_scale[0] = desc.uv_scale[1] = 1.0F;
      std::vector<content::ResourceKey> keys(6);
      keys[1] = normal;
      CHECK_F(
        mesh_node.GetRenderable().SetMaterialOverride(FirstSurfaceOf(mesh_node),
          std::make_shared<data::MaterialAsset>(
            data::AssetKey::FromVirtualPath("/test/IBL/Material-"
              + std::to_string(++material_sequence) + ".omat"),
            desc, std::vector<data::ShaderReference> {}, std::move(keys))),
        "the fixture mesh must declare the overridden surface");
    }

    //! Lights the scene from captures of a height fog the main view does not
    //! render.
    auto AddCapturedFog(const CapturedFog& authored = {}) -> Fog&
    {
      Sky().SetSource(SkyLightSource::kCapturedScene);
      auto& fog = scene->GetEnvironment()->AddSystem<Fog>();
      fog.SetEnabled(true);
      fog.SetEnableHeightFog(true);
      fog.SetFogDensity(authored.density);
      fog.SetHeightFalloffPerMeter(authored.falloff_per_meter);
      fog.SetMaxOpacity(authored.max_opacity);
      fog.SetFogInscatteringLuminance(authored.luminance);
      fog.SetRenderInMainPass(false);
      fog.SetVisibleInRealTimeSkyCaptures(true);
      return fog;
    }

    //! A rough white surface lit only by the irradiance of a captured fog, so
    //! the surface shows the fog luminance.
    auto UseCapturedFogIrradiance(const glm::vec3& luminance = glm::vec3(1.0F))
      -> Fog&
    {
      auto& fog = AddCapturedFog({ .luminance = luminance });
      Sky().SetSpecularIntensity(0.0F);
      Material(MaterialDomain::kOpaque, kRoughDielectric);
      return fog;
    }

    //! Publishes a captured-fog IBL generation with GPU validation off.
    auto PublishUnvalidatedCapturedFog() -> Fog&
    {
      auto& fog = UseCapturedFogIrradiance();
      Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kNone);
      RenderDeferred();
      return fog;
    }

    //! Edits the fog as authoring, which publishes on the next frame.
    auto AuthorFogLuminance(Fog& fog, const glm::vec3& luminance) -> void
    {
      fog.SetFogInscatteringLuminance(luminance);
      scene->NotifyEnvironmentAuthoringChange();
    }

    auto AddSkySphere() -> SkySphere&
    {
      auto& sphere = scene->GetEnvironment()->AddSystem<SkySphere>();
      sphere.SetEnabled(true);
      return sphere;
    }

    auto AddPrimarySun(const float lux) -> void
    {
      auto sun = scene->CreateNode("IBL sun");
      auto light = std::make_unique<scene::DirectionalLight>();
      light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
      light->SetIntensityLux(lux);
      ASSERT_TRUE(sun.AttachLight(std::move(light)));
    }

    //! Records the IBL products published to the surface view on each render.
    auto TrackPublishedProducts() -> void
    {
      probe->inspect = [this](const RenderContext& context,
                         const SceneTextureExtractRef& /*scene_textures*/,
                         unsigned /*draws*/) -> void {
        published_ = RendererPublicationProbe::PublishedIblProducts(
          *SceneRendererOf(), context.current_view.view_id);
      };
    }

    //! Acquires the surface view's IBL capture on each render.
    auto TrackCaptureAdmission() -> void
    {
      probe->inspect = [this](const RenderContext& context,
                         const SceneTextureExtractRef& /*scene_textures*/,
                         unsigned /*draws*/) -> void {
        admitted_view_ = context.current_view.view_id;
        admitted_ = renderer_->AcquireIblCapture(admitted_view_);
      };
    }

    auto ReadMetadata(const IblGpuProducts& products) -> IblProductMetadata
    {
      return Read<IblProductMetadata>(
        *products.metadata, ResourceStates::kShaderResource);
    }

    //! Test-only format comparison between completed frames. Preserves the
    //! products and generation; only `metadata` is replaced.
    auto OverwriteMetadata(const IblGpuProducts& products,
      const IblProductMetadata& metadata) -> void
    {
      auto upload = CreateUploadBuffer(SizeBytes { sizeof(metadata) });
      upload->Update(&metadata, sizeof(metadata), 0U);
      SubmitCommands("IBL metadata overwrite",
        [&](graphics::CommandRecorder& recorder) -> void {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
          EnsureTracked(recorder, upload, ResourceStates::kGenericRead);
          recorder.RequireResourceState(
            *products.metadata, ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBuffer(
            *products.metadata, 0U, *upload, 0U, sizeof(metadata));
          recorder.RequireResourceStateFinal(
            *products.metadata, ResourceStates::kShaderResource);
        });
      WaitForQueueIdle();
    }

    //! Reads `lease` directly; works after the renderer shut down.
    auto ReadRetained(const IblCaptureLease& lease) -> RetainedCapture
    {
      auto cube_readback
        = GetReadbackManager()->CreateTextureReadback("Retained IBL cube");
      auto metadata_readback
        = GetReadbackManager()->CreateBufferReadback("Retained IBL metadata");
      const auto face_size = lease.ProcessedCube()->GetDescriptor().width;
      graphics::testing::SubmitCommands(Backend(), "Retained IBL capture read",
        [&](graphics::CommandRecorder& recorder) -> void {
          CHECK_F(lease.Attach(recorder, Backend().GetResourceRegistry())
              .has_value());
          recorder.FlushBarriers();
          CHECK_F(cube_readback
                    ->EnqueueCopy(recorder, *lease.ProcessedCube(),
                      { .src_slice = {
                          .width = face_size,
                          .height = face_size,
                          .depth = 1U,
                        } })
                    .has_value());
          CHECK_F(metadata_readback
              ->EnqueueCopy(
                recorder, *lease.Metadata(), { 0U, sizeof(IblProductMetadata) })
              .has_value());
        });
      const auto cube = cube_readback->MapNow();
      const auto metadata = metadata_readback->MapNow();
      CHECK_F(cube.has_value() && metadata.has_value());
      auto result = RetainedCapture {};
      std::memcpy(
        result.first_texel.data(), cube->Data(), sizeof(result.first_texel));
      std::memcpy(
        &result.metadata, metadata->Bytes().data(), sizeof(result.metadata));
      return result;
    }

    //! Records the G-buffer inputs of each deferred render while `enabled`.
    auto CapturePackedInputs(const bool enabled) -> void
    {
      probe->after_render = {};
      packed_input_textures_.fill(nullptr);
      if (!enabled) {
        return;
      }
      probe->after_render
        = [this](const ViewRenderGpuContext& /*hook*/) -> void {
        auto* owner = SceneRendererOf();
        const auto& bindings = owner->GetSceneTextureBindings();
        auto& textures = owner->GetSceneTextures();
        // Capture while the view scope is active; SceneRenderer restores its
        // default resources when RenderSurface returns.
        packed_input_slots_ = {
          bindings.gbuffer_srvs[2].get(),
          bindings.gbuffer_srvs[1].get(),
          bindings.gbuffer_srvs[0].get(),
        };
        packed_input_textures_ = {
          textures.GetGBufferBaseColor().shared_from_this(),
          textures.GetGBufferMaterial().shared_from_this(),
          textures.GetGBufferNormal().shared_from_this(),
        };
      };
    }

    //! Samples the captured G-buffer inputs at the surface centre.
    auto PackedSurfaceInputs() -> GBufferTexels
    {
      // Reuse the typed texture-sampling probe. These are producer inputs,
      // not an invocation of the production IBL evaluator.
      const auto half = std::bit_cast<std::uint32_t>(0.5F);
      const auto one = std::bit_cast<std::uint32_t>(1.0F);
      constexpr std::uint32_t kPointClampSampler = 2U;
      constexpr std::uint32_t kTextureSampleMode = 512U;
      auto inputs = std::array<std::array<std::uint32_t, 4>, 6> {};
      for (std::size_t i = 0; i < packed_input_slots_.size(); ++i) {
        inputs.at(2 * i)
          = { packed_input_slots_.at(i), kPointClampSampler, half, half };
        inputs.at((2 * i) + 1) = { one, 0, 0, 0 };
      }
      const auto values = RunToneProbe(std::as_bytes(std::span(inputs)),
        static_cast<std::uint32_t>(packed_input_slots_.size()),
        kTextureSampleMode, false,
        [&](graphics::CommandRecorder& recorder) -> void {
          for (const auto& texture : packed_input_textures_) {
            CHECK_F(recorder.AdoptKnownResourceState(*texture));
            recorder.RequireResourceState(
              *texture, ResourceStates::kShaderResource);
          }
        });
      const auto texel = [&](const std::size_t i) -> Pixel {
        return {
          values.at(i)[0],
          values.at(i)[1],
          values.at(i)[2],
          values.at(i)[3],
        };
      };
      return {
        .base_color = texel(0),
        .material = texel(1),
        .normal = texel(2),
      };
    }

    //! Replaces the surface with a camera-facing triangle, reversed in winding
    //! and authored normal when `backface`.
    auto UseTriangleReceiver(const bool backface) -> void
    {
      const std::array positions {
        glm::vec3 { -2, -2, -1 },
        glm::vec3 { 2, -2, -1 },
        glm::vec3 { 0, 2, -1 },
      };
      const float facing = backface ? -1.0F : 1.0F;
      std::vector<data::Vertex> vertices;
      vertices.reserve(positions.size());
      for (const auto& position : positions) {
        vertices.push_back({
          .position = position,
          .normal = { 0, 0, facing },
          .texcoord = { 0.5F, 0.5F },
          .tangent = { 1, 0, 0 },
          .bitangent = { 0, facing, 0 },
          .color = { 1, 1, 1, 1 },
        });
      }
      auto mesh
        = data::MeshBuilder()
            .WithVertices(vertices)
            .WithIndices(backface ? std::vector<std::uint32_t> { 0, 2, 1 }
                                  : std::vector<std::uint32_t> { 0, 1, 2 })
            .BeginSubMesh("IBL receiver", data::MaterialAsset::CreateDefault())
            .WithMeshView({
              .first_index = 0,
              .index_count = 3,
              .first_vertex = 0,
              .vertex_count = 3,
            })
            .EndSubMesh()
            .Build();
      data::pak::geometry::GeometryAssetDesc desc {};
      desc.lod_count = 1;
      desc.bounding_box_min[0] = desc.bounding_box_min[1] = -2;
      desc.bounding_box_max[0] = desc.bounding_box_max[1] = 2;
      desc.bounding_box_min[2] = desc.bounding_box_max[2] = -1;
      mesh_node.GetRenderable().SetGeometry(
        std::make_shared<data::GeometryAsset>(
          data::AssetKey::FromVirtualPath("/test/IBL/Receiver-"
            + std::to_string(++material_sequence) + ".ogeo"),
          desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) }));
    }

    //! Renders a `width` x `height` image through the view's own framebuffer.
    auto UseImageView(const std::uint32_t width, const std::uint32_t height)
      -> void
    {
      view.viewport.width = static_cast<float>(width);
      view.viewport.height = static_cast<float>(height);
      auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
      ASSERT_HAS_VALUE(lens);
      lens->get().SetAspectRatio(
        static_cast<float>(width) / static_cast<float>(height));
      lens->get().SetViewport(view.viewport);
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(
          CreateRegisteredTexture({
            .width = width,
            .height = height,
            .format = Format::kRGBA32Float,
            .is_render_target = true,
            .initial_state = ResourceStates::kCommon,
          })));
      probe->prepare = [](RenderContext& /*context*/) -> void { };
      verify_manual_p = false;
    }

    //! Renders through the runtime publication (`published`) or offscreen.
    auto RenderImage(const bool published, const bool forward, const float ev,
      const unsigned frames) -> void
    {
      settings.manual_ev = ev;
      if (published) {
        // The published fixture expects resident geometry/materials. Warm
        // the shared uploads first; the measured frame still uses
        // publication.
        if (frames > 1) {
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, frames));
        }
        probe->color.reset();
        ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
      } else {
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, frames));
      }
      ASSERT_NE(probe->color, nullptr);
    }

    //! The rendered surface image divided by its pre-exposure.
    auto LinearImage() -> std::vector<Pixel>
    {
      const auto domain = Read<FrameExposureData>(
        *probe->exposure->buffer, ResourceStates::kShaderResource);
      CHECK_F(std::isfinite(domain.pre_exposure) && domain.pre_exposure > 0);
      auto pixels = ReadFloatTexture(*probe->color);
      for (auto& pixel : pixels) {
        for (unsigned channel = 0; channel < 3; ++channel) {
          pixel[channel] /= domain.pre_exposure;
        }
      }
      return pixels;
    }

    auto DisplayImage() -> std::vector<Pixel>
    {
      return ReadFloatTexture(
        *framebuffer->GetDescriptor().color_attachments.front().texture);
    }

    //! Renders the surface with the sky light off, then on.
    auto RenderSkyContribution(const bool published, const bool forward,
      const float ev, SkyContribution& contribution) -> void
    {
      Sky().SetIntensityMul(0);
      ASSERT_NO_FATAL_FAILURE(RenderImage(published, forward, ev, 5));
      contribution.baseline = LinearImage();
      Sky().SetIntensityMul(1);
      ASSERT_NO_FATAL_FAILURE(RenderImage(published, forward, ev, 1));
      contribution.lit = LinearImage();
      ASSERT_EQ(contribution.baseline.size(), contribution.lit.size());
    }

    std::shared_ptr<const IblGpuProducts> published_;
    CaptureResult admitted_ { Err(IblCaptureError::kUnavailable) };
    ViewId admitted_view_ { kInvalidViewId };
    std::array<std::uint32_t, 3> packed_input_slots_ {};
    std::array<std::shared_ptr<graphics::Texture>, 3> packed_input_textures_;
  };

  class IblDiagnosticsGpuTest : public IblSurfaceGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling;
    }
  };

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, MetadataCollectionFollowsFrameDiagnostics)
  {
    auto& fog = PublishUnvalidatedCapturedFog();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kNotRequested);

    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kPending);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    const auto state = SkyState();
    EXPECT_EQ(state.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(state.validated_revision, state.published_revision);
    EXPECT_FLOAT_EQ(state.source_radiance_scale, 1.0F);
    EXPECT_NEAR(state.average_brightness, 1.0F, 0.0001F);
    EXPECT_EQ(state.last_failed_gpu_revision, 0U);
    EXPECT_FALSE(Diagnostics().IsGpuTimelineEnabled());

    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kNone);
    AuthorFogLuminance(fog, glm::vec3(4.0F));
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kNotRequested);
    EXPECT_NEAR(SurfaceColor()[0], 4.0F, 0.001F);

    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(2U));
    const auto revalidated = SkyState();
    EXPECT_EQ(revalidated.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_NEAR(revalidated.average_brightness, 4.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, GpuInvalidGenerationIsZeroAndRemainsVisible)
  {
    ASSERT_NO_FATAL_FAILURE(PublishUnvalidatedCapturedFog());
    const auto products = IblOwner().GetPublishedProducts();
    ASSERT_TRUE(products);
    auto metadata = ReadMetadata(*products);
    // Complete but not finite.
    metadata.processing_flags = environment::kIblProductComplete;
    OverwriteMetadata(*products, metadata);

    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kPending);
    EXPECT_EQ(SurfaceColor()[0], 0.0F);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    const auto state = SkyState();
    EXPECT_TRUE(state.usable); // CPU submission was accepted; GPU rejected it.
    EXPECT_EQ(state.gpu_validation, SkyLightGpuValidation::kInvalid);
    EXPECT_EQ(state.validated_revision, products->revision);
    EXPECT_EQ(state.last_failed_gpu_revision, products->revision);
    EXPECT_TRUE(HasIssue("ibl.gpu-invalid", DiagnosticsSeverity::kError));

    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kNone);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(2U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kInvalid);
    EXPECT_EQ(SurfaceColor()[0], 0.0F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, LateInvalidObservationDoesNotRejectNewGeneration)
  {
    auto& fog = PublishUnvalidatedCapturedFog();
    ASSERT_FALSE(HasFatalFailure());
    const auto products = IblOwner().GetPublishedProducts();
    ASSERT_TRUE(products);
    auto metadata = ReadMetadata(*products);
    metadata.product_revision = 0U;
    OverwriteMetadata(*products, metadata);
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    AuthorFogLuminance(fog, glm::vec3(8.0F));
    // Frame start requests the old generation; authoring publishes its
    // successor.
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    ASSERT_NE(SkyState().published_revision, products->revision);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    const auto pending = SkyState();
    EXPECT_EQ(pending.last_failed_gpu_revision, products->revision);
    EXPECT_EQ(pending.gpu_validation, SkyLightGpuValidation::kPending);
    EXPECT_EQ(pending.validated_revision, 0U);
    EXPECT_NEAR(SurfaceColor()[0], 8.0F, 0.001F);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    const auto valid = SkyState();
    EXPECT_EQ(valid.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(valid.validated_revision, valid.published_revision);
    EXPECT_EQ(valid.last_failed_gpu_revision, products->revision);
    EXPECT_NEAR(valid.average_brightness, 8.0F, 0.001F);
  }

#if defined(_MSC_VER) && defined(_DEBUG)
  NOLINT_TEST_F(IblDiagnosticsGpuTest,
    DiagnosticAllocationFailurePreservesLightingAndRetries)
  {
    ASSERT_NO_FATAL_FAILURE(PublishUnvalidatedCapturedFog());
    const auto products = IblOwner().GetPublishedProducts();
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    {
      graphics::testing::HeapAllocationFailure denied;
      // Only the outcome matters: the request fails without touching the
      // published generation.
      static_cast<void>(
        IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U }));
    }
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kUnavailable);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(3U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.001F);
  }
#endif

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, RejectedMetadataCopyPreservesLightingAndRetries)
  {
    ASSERT_NO_FATAL_FAILURE(PublishUnvalidatedCapturedFog());
    const auto products = IblOwner().GetPublishedProducts();
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    graphics::internal::SubmissionFaultTestAccess::FailNext(
      *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
    EXPECT_FALSE(
      IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U }));
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kUnavailable);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(3U));
    EXPECT_EQ(SkyState().gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_EQ(IblOwner().GetPublishedProducts(), products);
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, UncertainMetadataCopyInvalidatesPublication)
  {
    ASSERT_NO_FATAL_FAILURE(PublishUnvalidatedCapturedFog());
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    graphics::internal::SubmissionFaultTestAccess::FailNext(*GetQueue(),
      graphics::internal::SubmissionFailurePoint::kAfterIssueBeforeMarker);
    const auto backend = Backend().GetBackendLifetime();
    const ScopeGuard restore(
      [backend] noexcept -> void { backend->ClearSubmissionFault(); });
    EXPECT_TRUE(
      IblOwner().OnFrameStart(frame::SequenceNumber { sequence + 1U }));
    EXPECT_EQ(IblOwner().GetPublishedProducts(), nullptr);
    EXPECT_FALSE(SkyState().usable);
    backend->ClearSubmissionFault();
    EXPECT_EQ(IblOwner().GetPublishedProducts(), nullptr);
    WaitForQueueIdle();
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, CapturedSceneLightsWithTheSkySphereImportedRadiance)
  {
    Sky().SetSource(SkyLightSource::kCapturedScene);
    Sky().SetSpecularIntensity(0.0F);
    Material(MaterialDomain::kOpaque, kRoughDielectric);
    auto& sphere = AddSkySphere();
    sphere.SetSource(scene::environment::SkySphereSource::kSolidColor);
    sphere.SetSolidColorRgb(glm::vec3(1.0F));
    // Display-only scaling never reaches lighting.
    sphere.SetIntensity(5.0F);
    sphere.SetTintRgb(glm::vec3(0.5F));
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto solid = SkyState();
    EXPECT_TRUE(solid.usable);
    EXPECT_FALSE(solid.empty_capture);
    EXPECT_EQ(solid.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_NEAR(solid.average_brightness, 1.0F, 0.0001F);
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.001F);

    sphere.SetIntensity(0.25F);
    sphere.SetTintRgb(glm::vec3(2.0F));
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_EQ(SkyState().published_revision, solid.published_revision);

    sphere.SetSource(scene::environment::SkySphereSource::kCubemap);
    sphere.SetCubemapResource(AddUniformCube({ 2, 2, 2, 1 }));
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto cube = SkyState();
    EXPECT_TRUE(cube.usable);
    EXPECT_NE(cube.published_revision, solid.published_revision);
    EXPECT_EQ(cube.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_NEAR(cube.average_brightness, 2.0F, 0.0001F);
    EXPECT_NEAR(SurfaceColor()[0], 2.0F, 0.001F);

    // The uniform cube delivers 2 pi lux upward; calibrating it to 4 pi lux
    // doubles the radiance that lights the scene.
    sphere.SetIlluminanceLux(4.0F * std::numbers::pi_v<float>);
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto calibrated = SkyState();
    EXPECT_EQ(calibrated.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_NEAR(calibrated.average_brightness, 4.0F, 0.01F);
    EXPECT_NEAR(SurfaceColor()[0], 4.0F, 0.01F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, SpecifiedCubemapIsCalibratedToItsIlluminance)
  {
    Sky().SetSpecularIntensity(0.0F);
    Material(MaterialDomain::kOpaque, kRoughDielectric);
    Sky().SetCubemapResource(AddUniformCube({ 2, 2, 2, 1 }));
    // The uniform cube delivers 2 pi lux; a pi lux target halves it.
    Sky().SetIlluminanceLux(std::numbers::pi_v<float>);
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto state = SkyState();
    EXPECT_TRUE(state.usable);
    EXPECT_NEAR(state.average_brightness, 1.0F, 0.01F);
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.01F);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, UnmeasurableSkySphereCalibrationIsReported)
  {
    Sky().SetSource(SkyLightSource::kCapturedScene);
    auto& sphere = AddSkySphere();
    sphere.SetSource(scene::environment::SkySphereSource::kCubemap);
    sphere.SetCubemapResource(
      AddUniformCube({ 1, 1, 1, 1 }, Format::kRGBA8UNorm));
    sphere.SetIlluminanceLux(10000.0F);
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_TRUE(HasIssue("sky.uncalibrated", DiagnosticsSeverity::kWarning));
    // LDR radiance never lights the scene, calibrated or not.
    EXPECT_TRUE(HasIssue("ibl.unavailable"));

    sphere.SetIlluminanceLux(0.0F);
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_FALSE(HasIssue("sky.uncalibrated"));
  }

  NOLINT_TEST_F(IblDiagnosticsGpuTest, UnavailableSkyLightIsReportedEachFrame)
  {
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    EXPECT_FALSE(SkyState().usable);
    const auto snapshot = Diagnostics().GetLatestSnapshot();
    EXPECT_TRUE(std::ranges::any_of(
      snapshot.issues, [](const DiagnosticsIssue& issue) -> bool {
        return issue.code == "ibl.unavailable"
          && issue.message.contains("has no cubemap");
      }));
  }

  //! IBL producer time reaches the GPU work summary as frame work, only while
  //! a viewer keeps collection on, and every scope of the frame is known to
  //! the stage table.
  NOLINT_TEST_F(IblDiagnosticsGpuTest, GpuWorkSummaryReportsIblAsFrameWork)
  {
    auto& fog = UseCapturedFogIrradiance();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    // Budget-only collection publishes no summary.
    for (unsigned edit = 0U; edit < 3U; ++edit) {
      AuthorFogLuminance(fog, glm::vec3(static_cast<float>(edit + 10U)));
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    }
    EXPECT_EQ(Diagnostics().GetLatestGpuWorkSummary(), nullptr);

    auto lease = Diagnostics().AcquireGpuTimeline();
    ASSERT_TRUE(lease.IsActive());
    Diagnostics().SetGpuTimelineMaxScopesPerFrame(1024U);
    const auto find_ibl
      = [](const GpuWorkSummary& summary) -> const GpuWorkItem* {
      for (const auto& group : summary.frame_work) {
        if (group.bucket != GpuWorkBucket::kBackground) {
          continue;
        }
        const auto found = std::ranges::find(
          group.items, std::string_view { "background.ibl" }, &GpuWorkItem::id);
        if (found != group.items.end()) {
          return &*found;
        }
      }
      return nullptr;
    };
    // A window closes after 500 ms of steady-clock time; keep the producer
    // busy with authoring edits until one has.
    auto summary = std::shared_ptr<const GpuWorkSummary> {};
    const auto deadline
      = std::chrono::steady_clock::now() + std::chrono::seconds { 10 };
    for (unsigned edit = 0U; std::chrono::steady_clock::now() < deadline;
      ++edit) {
      AuthorFogLuminance(fog, glm::vec3(static_cast<float>((edit % 8U) + 1U)));
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      summary = Diagnostics().GetLatestGpuWorkSummary();
      if (summary && find_ibl(*summary) != nullptr) {
        break;
      }
    }
    ASSERT_NE(summary, nullptr);
    EXPECT_GT(summary->valid_frames, 0U);
    const auto* ibl = find_ibl(*summary);
    ASSERT_NE(ibl, nullptr);
    EXPECT_GT(ibl->time.avg_ms, 0.0F);
    EXPECT_GE(ibl->time.max_ms, ibl->time.avg_ms);
    // Every telemetry scope of this lit, IBL-processing frame has a stage
    // table entry: kOther holds only command list work outside stages.
    const auto expect_known = [](const GpuWorkGroup& group) -> void {
      if (group.bucket != GpuWorkBucket::kOther) {
        return;
      }
      for (const auto& item : group.items) {
        EXPECT_TRUE(item.id.starts_with("list:") || item.id == "other")
          << item.id;
      }
    };
    for (const auto& work_view : summary->views) {
      for (const auto& group : work_view.groups) {
        EXPECT_NE(group.bucket, GpuWorkBucket::kBackground) << work_view.name;
        expect_known(group);
      }
    }
    for (const auto& group : summary->frame_work) {
      expect_known(group);
    }

    lease.Release();
    EXPECT_EQ(Diagnostics().GetLatestGpuWorkSummary(), nullptr);
  }

  NOLINT_TEST_F(
    IblDiagnosticsGpuTest, MetadataReadbackStorageStabilizesAcrossAuthoring)
  {
    // Readback storage grows until a full ring of generations is in flight.
    constexpr unsigned kEdits = 24U;
    constexpr unsigned kSteadyEdit = 7U;
    auto& fog = UseCapturedFogIrradiance();
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kFrameLedger);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    std::size_t registrations = 0U;
    float luminance = 1.0F;
    for (unsigned edit = 0U; edit < kEdits; ++edit) {
      luminance = static_cast<float>(edit + 2U);
      AuthorFogLuminance(fog, glm::vec3(luminance));
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      Backend().PollCompletedUses();
      const auto current
        = Backend().GetResourceRegistry().GetRegisteredResourceCount();
      if (edit == kSteadyEdit) {
        registrations = current;
      }
      if (edit > kSteadyEdit) {
        EXPECT_EQ(current, registrations);
      }
      EXPECT_EQ(SkyState().last_failed_gpu_revision, 0U);
    }
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(3U));
    const auto state = SkyState();
    EXPECT_EQ(state.gpu_validation, SkyLightGpuValidation::kValid);
    EXPECT_NEAR(state.average_brightness, luminance, 0.001F);
    RecordProperty("authoring_generations", kEdits);
    RecordProperty("steady_registered_resources", registrations);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, SplitSumAndIndependentControlsAcrossNativeSurfacePaths)
  {
    constexpr Pixel radiance { 0.25F, 0.5F, 1.0F, 1.0F };
    Sky().SetCubemapResource(AddUniformCube(radiance));
    // Exact endpoints isolate IBL from sRGB GBuffer quantization. Zero green
    // also exercises the retained F90=50*F0.g rule on a colored metal.
    const std::array colors {
      glm::vec3 { 1.0F, 0.0F, 1.0F },
      glm::vec3 { 0.8F, 0.4F, 0.2F },
    };
    const std::array controls {
      SkyControls { .diffuse = 0, .specular = 0 },
      SkyControls { .specular = 0 },
      SkyControls { .diffuse = 0 },
      SkyControls {},
      SkyControls { .reflections = false },
      SkyControls {
        .intensity = 2,
        .diffuse = 0.25F,
        .specular = 1.75F,
        .tint = { 0.5F, 1, 0.25F },
      },
    };
    // The capture case is one fixed, representative frame.
    constexpr unsigned kCapturedCase = 6U;
    // Half an 8-bit code.
    constexpr float kUnorm8Tolerance = (0.5F / 255.0F) + 1.0e-6F;
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kAllDomains) {
        const bool deferred_shading
          = !forward && domain != MaterialDomain::kAlphaBlended;
        CapturePackedInputs(deferred_shading);
        for (const auto& albedo : colors) {
          for (const float metallic : { 0.0F, 1.0F }) {
            for (const float roughness : { 0.0F, 0.2F, 1.0F }) {
              Material(domain, { .roughness = roughness, .metallic = metallic },
                albedo);
              // Deferred shading evaluates the G-buffer encoding of the
              // material, read after its first render.
              auto surface = SurfaceInputs {
                .color = albedo,
                .metallic = metallic,
                .specular = 0.5F,
                .roughness = roughness,
              };
              bool read_packed_inputs = deferred_shading;
              bool warm_material = true;
              for (const auto& control : controls) {
                ApplySky(control);
                for (const float ev : { 0.0F, 3.0F }) {
                  SCOPED_TRACE(::testing::Message()
                    << "case=" << cases << " forward=" << forward
                    << " domain=" << to_string(domain)
                    << " metallic=" << metallic << " roughness=" << roughness
                    << " diffuse=" << control.diffuse
                    << " specular=" << control.specular
                    << " reflections=" << control.reflections << " ev=" << ev);
                  // Only asset residency is warmed up. Gain/tint/lobe and
                  // exposure edits must reach the very next rendered frame.
                  const auto capture = cases == kCapturedCase
                    ? BeginOptionalCapture()
                    : observer_ptr<graphics::FrameCaptureController> {};
                  ASSERT_NO_FATAL_FAILURE(
                    RenderSurface(forward, ev, warm_material ? 5U : 1U));
                  warm_material = false;
                  if (capture) {
                    EXPECT_TRUE(capture->EndCapture());
                  }
                  const auto pixel = SurfaceColor();
                  if (read_packed_inputs) {
                    const auto packed = PackedSurfaceInputs();
                    for (unsigned c = 0; c < 3; ++c) {
                      EXPECT_NEAR(packed.base_color[c],
                        albedo[static_cast<glm::length_t>(c)], 0.01F);
                    }
                    EXPECT_FLOAT_EQ(packed.material[0], metallic);
                    EXPECT_NEAR(packed.material[1], 0.5F, kUnorm8Tolerance);
                    EXPECT_NEAR(
                      packed.material[2], roughness, kUnorm8Tolerance);
                    surface = DecodeGBuffer(packed);
                    read_packed_inputs = false;
                  }
                  const double scale
                    = Coverage(domain) * std::exp2(-static_cast<double>(ev));
                  for (unsigned c = 0; c < 3; ++c) {
                    const double expected
                      = SplitSum(radiance, surface, control, c) * scale;
                    EXPECT_NEAR(pixel[c], expected,
                      (std::abs(expected) * 0.001) + 1.0e-6);
                  }
                  EXPECT_FLOAT_EQ(
                    pixel[3], static_cast<float>(Coverage(domain)));
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
    Sky().SetCubemapResource(AddCube(16U, VerticalGradient(16U)));
    Sky().SetSpecularIntensity(0.0F);
    const auto normal = AddTexel(kTiltedNormal);
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kAllDomains) {
        for (const bool backface : { false, true }) {
          UseTriangleReceiver(backface);
          for (const bool mapped : { false, true }) {
            Material(domain, kRoughDielectric, kWhite,
              mapped ? normal : content::ResourceKey {});
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << to_string(domain)
              << " backface=" << backface << " mapped=" << mapped);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto pixel = SurfaceColor();
            // Reversed winding has matching authored N/B. Double-sided
            // evaluation must orient the visible normal toward the camera.
            // The gradient's cosine-weighted irradiance is
            // base + 2/3 * slope * N.z.
            const double normal_z = mapped ? kTiltedNormalZ : 1.0;
            for (unsigned c = 0; c < 3; ++c) {
              const double expected
                = (kGradientBase.at(c)
                    + ((2.0 / 3.0) * kGradientSlope.at(c) * normal_z))
                * Coverage(domain);
              EXPECT_NEAR(
                pixel[c], expected, (std::abs(expected) * 0.002) + 1.0e-6);
            }
            EXPECT_FLOAT_EQ(pixel[3], static_cast<float>(Coverage(domain)));
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
    Sky().SetCubemapResource(AddCube(32U, VerticalGradient(32U)));
    Sky().SetDiffuseIntensity(0.0F);
    const auto normal = AddTexel(kTiltedNormal);
    // Glossy flat, glossy mapped, rough flat and rough mapped reflections of
    // deferred opaque shading, which every other path must reproduce.
    std::array<Pixel, 4> reference {};
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kAllDomains) {
        const auto coverage = static_cast<float>(Coverage(domain));
        std::array<Pixel, 4> actual {};
        unsigned index = 0;
        for (const float roughness : { 0.0F, 1.0F }) {
          for (const bool mapped : { false, true }) {
            Material(domain, { .roughness = roughness, .metallic = 1.0F },
              kWhite, mapped ? normal : content::ResourceKey {});
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << to_string(domain)
              << " roughness=" << roughness << " mapped=" << mapped);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            actual.at(index) = SurfaceColor();
            for (unsigned c = 0; c < 3; ++c) {
              actual.at(index)[c] /= coverage;
            }
            if (!forward && domain == MaterialDomain::kOpaque) {
              reference.at(index) = actual.at(index);
            } else {
              for (unsigned c = 0; c < 3; ++c) {
                // The deferred shading normal is octahedrally packed in
                // UNORM10.
                EXPECT_NEAR(actual.at(index)[c], reference.at(index)[c],
                  (reference.at(index)[c] * 0.01F) + 1.0e-5F);
              }
            }
            EXPECT_FLOAT_EQ(actual.at(index)[3], coverage);
            ++index;
            ++cases;
          }
        }
        // A tilted normal reflects the dimmer horizon; roughness spreads the
        // bright zenith over a wider lobe.
        EXPECT_LT(actual[1][0], actual[0][0] * 0.95F);
        EXPECT_GT(actual[0][0], actual[2][0] * 2.0F);
      }
    }
    RecordProperty("ibl_raster_reflection_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, CapturedHeightFogLightsSurfacesWithoutMainViewFog)
  {
    constexpr Pixel luminance { 128, 64, 32, 1 };
    auto& fog = AddCapturedFog(
      { .luminance = { luminance[0], luminance[1], luminance[2] } });
    constexpr SkyControls specular_only { .diffuse = 0.0F };
    ApplySky(specular_only);
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kOpaqueAndBlended) {
        for (const float roughness : { 0.0F, 1.0F }) {
          Material(domain, { .roughness = roughness, .metallic = 1.0F });
          const auto surface
            = SurfaceInputs { .metallic = 1.0F, .roughness = roughness };
          for (const float volumetric : { 0.0F, 4.0F }) {
            Sky().SetVolumetricScatteringIntensity(volumetric);
            SCOPED_TRACE(::testing::Message()
              << "forward=" << forward << " domain=" << to_string(domain)
              << " roughness=" << roughness << " volumetric=" << volumetric);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto pixel = SurfaceColor();
            for (unsigned c = 0; c < 3; ++c) {
              const double expected
                = SplitSum(luminance, surface, specular_only, c)
                * Coverage(domain);
              EXPECT_NEAR(pixel[c], expected, (expected * 0.001) + 1.0e-5);
            }
            EXPECT_FLOAT_EQ(pixel[3], static_cast<float>(Coverage(domain)));
            ++cases;
          }
        }
      }
    }

    // A glossy metal mirrors the capture.
    Material(MaterialDomain::kOpaque, { .roughness = 0.0F, .metallic = 1.0F });
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const glm::vec3 edited_luminance { 64, 128, 32 };
    AuthorFogLuminance(fog, edited_luminance);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    const auto edited = SurfaceColor();
    for (unsigned c = 0; c < 3; ++c) {
      const auto wanted = edited_luminance[static_cast<glm::length_t>(c)];
      EXPECT_NEAR(edited[c], wanted, wanted / 32000.0F);
    }
    ++cases;

    fog.SetVisibleInRealTimeSkyCaptures(false);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    ExpectUnlit(SurfaceColor());
    ++cases;

    fog.SetVisibleInRealTimeSkyCaptures(true);
    Sky().SetEnabled(false);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    ExpectUnlit(SurfaceColor());
    ++cases;
    RecordProperty("ibl_captured_surface_cases", cases);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, SpecularHdrScaleAndTinyChannelsSurviveRasterTransport)
  {
    constexpr Pixel source { 0x1p20F, 0x1p-24F, 1.0F, 1.0F };
    Sky().SetCubemapResource(AddUniformCube(source));
    Sky().SetDiffuseIntensity(0.0F);
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kAllDomains) {
        for (const float roughness : { 0.0F, 1.0F }) {
          Material(domain, { .roughness = roughness, .metallic = 1.0F });
          const auto surface
            = SurfaceInputs { .metallic = 1.0F, .roughness = roughness };
          for (const float gain : { 1.0F, 1024.0F }) {
            const auto controls
              = SkyControls { .intensity = gain, .diffuse = 0.0F };
            ApplySky(controls);
            for (const float ev : { -4.0F, 0.0F, 16.0F }) {
              SCOPED_TRACE(::testing::Message()
                << "forward=" << forward << " domain=" << to_string(domain)
                << " roughness=" << roughness << " gain=" << gain
                << " ev=" << ev);
              ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
              const auto pixel = SurfaceColor();
              for (unsigned c = 0; c < 3; ++c) {
                const double expected = SplitSum(source, surface, controls, c)
                  * Coverage(domain) * std::exp2(-static_cast<double>(ev));
                EXPECT_NEAR(pixel[c], expected, (expected * 2.0e-5) + 0x1p-120);
              }
              EXPECT_FLOAT_EQ(pixel[3], static_cast<float>(Coverage(domain)));
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
    AddCapturedFog({
      .luminance = { 0.23F, 0.41F, 0.61F },
      .density = 0.001F,
      .falloff_per_meter = 0.001F,
      .max_opacity = 0.75F,
    });
    Sky().SetDiffuseIntensity(0.0F);
    scene->GetEnvironment()
      ->AddSystem<scene::environment::SkyAtmosphere>()
      .SetEnabled(true);
    ASSERT_NO_FATAL_FAILURE(AddPrimarySun(1000.0F));
    Diagnostics().SetHdrFp32ReferenceEnabled(true);
    TrackPublishedProducts();
    auto maximum_rgb_error = 0.0;
    auto maximum_ev_error = 0.0;
    unsigned changed_channels = 0U;
    unsigned cases = 0U;
    for (const bool forward : { false, true }) {
      for (const auto domain : kAllDomains) {
        for (const float roughness : { 0.0F, 0.1F, 0.5F, 1.0F }) {
          for (const auto tint :
            { glm::vec3 { 1, 1, 1 }, glm::vec3 { 0, 1, 0 } }) {
            SCOPED_TRACE(::testing::Message() << "case=" << cases);
            Material(domain, { .roughness = roughness, .metallic = 1.0F });
            Sky().SetTintRgb(tint);
            Sky().SetSpecularIntensity(0.0F);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
            const auto baseline = SurfaceColor();
            const auto products = published_;
            ASSERT_NE(products, nullptr);
            const auto metadata = ReadMetadata(*products);
            ASSERT_EQ(metadata.precision_flags,
              environment::kIblHalfCertificateComplete);
            ASSERT_GT(metadata.maximum_half_gain, 1.0F);

            // The same generation, sampled first as canonical FP32 and then
            // through its certified half-precision products.
            Sky().SetSpecularIntensity(1.0F);
            auto canonical_metadata = metadata;
            canonical_metadata.precision_flags = 0U;
            const auto capture = cases == 0U
              ? BeginOptionalCapture()
              : observer_ptr<graphics::FrameCaptureController> {};
            OverwriteMetadata(*products, canonical_metadata);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
            const auto reference = SurfaceColor();
            OverwriteMetadata(*products, metadata);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
            const auto actual = SurfaceColor();
            if (capture) {
              EXPECT_TRUE(capture->EndCapture());
            }
            ASSERT_NE(published_, nullptr);
            EXPECT_EQ(published_.get(), products.get());
            EXPECT_EQ(published_->revision, products->revision);
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double a
                = static_cast<double>(actual[channel]) - baseline[channel];
              const double r
                = static_cast<double>(reference[channel]) - baseline[channel];
              if (tint[static_cast<glm::length_t>(channel)] == 0.0F) {
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
              if (a != r) {
                ++changed_channels;
              }
            }
            EXPECT_EQ(actual[3], reference[3]);
            ++cases;
          }
        }
      }
    }
    EXPECT_GT(changed_channels, 0U);
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
    constexpr unsigned kBatches = 12U;
    auto& fog = UseCapturedFogIrradiance({ 1, 2, 3 });
    TrackPublishedProducts();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    ASSERT_NE(published_, nullptr);
    for (unsigned edit = 1U; edit <= kBatches; ++edit) {
      SCOPED_TRACE(edit);
      const auto previous = published_->revision;
      const auto edit_value = static_cast<float>(edit);
      const glm::vec3 wanted {
        edit_value,
        0.5F * edit_value,
        0.25F * edit_value,
      };
      AuthorFogLuminance(fog, wanted * 3.0F);
      AuthorFogLuminance(fog, wanted);
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      ASSERT_NE(published_, nullptr);
      const auto actual = SurfaceColor();
      for (unsigned c = 0U; c < 3U; ++c) {
        const auto component = wanted[static_cast<glm::length_t>(c)];
        EXPECT_NEAR(actual[c], component, component * 0.0001F);
      }
      EXPECT_EQ(published_->revision, previous + 1U);
      EXPECT_EQ(scene->GetEnvironmentAuthoringRevision(), 2U * edit);
    }
    RecordProperty("immediate_authoring_batches", kBatches);
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, RuntimeFogUpdatesPublishWithoutStarvation)
  {
    constexpr unsigned kEdits = 16U;
    constexpr unsigned kSettleFrames = 4U;
    auto& fog = UseCapturedFogIrradiance();
    const auto first_snapshot_frame = sequence + 1U;
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto warmup_end = sequence;
    const auto capture = BeginOptionalCapture();
    float last_published = 1.0F;
    unsigned publications = 0U;
    unsigned longest_wait = 0U;
    unsigned wait = 0U;
    for (unsigned edit = 1U; edit <= kEdits; ++edit) {
      SCOPED_TRACE(edit);
      // Runtime edits, unlike authoring, publish when a capture completes.
      const auto wanted = static_cast<float>(edit + 1U);
      fog.SetFogInscatteringLuminance(glm::vec3(wanted));
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      const auto actual = SurfaceColor()[0];
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
      const auto status = SkyState();
      EXPECT_TRUE(status.observed);
      EXPECT_TRUE(status.usable);
      EXPECT_EQ(status.scene_lifetime, scene->GetLifetimeId().get());
      EXPECT_EQ(status.frame_sequence, sequence);
      // The published luminance names the frame whose snapshot it shows.
      const auto snapshot_frame = last_published < 1.5F
        ? first_snapshot_frame
        : warmup_end + static_cast<unsigned>(std::lround(last_published)) - 1U;
      EXPECT_EQ(status.source_age_frames, sequence - snapshot_frame);
      if (edit == 4U && capture) {
        EXPECT_TRUE(capture->EndCapture());
      }
    }
    EXPECT_GE(publications, 4U);
    EXPECT_LE(longest_wait, 4U);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(kSettleFrames));
    EXPECT_NEAR(SurfaceColor()[0], static_cast<float>(kEdits + 1U), 0.002F);
    const auto current = SkyState();
    EXPECT_EQ(current.source_age_frames, 0U);
    EXPECT_EQ(current.building_revision, 0U);
    const auto other = std::make_shared<scene::Scene>("Unrendered scene", 8U);
    EXPECT_FALSE(renderer_->InspectSkyLight(*other).observed);
    RecordProperty("runtime_frames", kEdits + kSettleFrames);
    RecordProperty("runtime_publications", publications);
    RecordProperty("maximum_publication_gap", longest_wait);
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, AuthoringPreemptsRuntimeCandidate)
  {
    auto& fog = UseCapturedFogIrradiance();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    fog.SetFogInscatteringLuminance(glm::vec3(2.0F));
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(2U));
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.001F);
    AuthorFogLuminance(fog, glm::vec3(8.0F));
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_NEAR(SurfaceColor()[0], 8.0F, 0.001F);
    for (unsigned frame_index = 0U; frame_index < 6U; ++frame_index) {
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      EXPECT_NEAR(SurfaceColor()[0], 8.0F, 0.001F);
    }
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, UnchangedKeyConsumesAuthoringIntent)
  {
    auto& fog = UseCapturedFogIrradiance();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    scene->NotifyEnvironmentAuthoringChange();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    fog.SetFogInscatteringLuminance(glm::vec3(4.0F));
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    EXPECT_NEAR(SurfaceColor()[0], 1.0F, 0.001F);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(3U));
    EXPECT_NEAR(SurfaceColor()[0], 4.0F, 0.001F);
  }

  NOLINT_TEST_F(
    IblSurfaceGpuTest, WorkBudgetLearnsWithDiagnosticsDisabledThenIdles)
  {
    auto& fog = UseCapturedFogIrradiance();
    Diagnostics().SetEnabledFeatures(DiagnosticsFeature::kNone);
    Diagnostics().SetGpuTimelineEnabled(false);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    auto& owner = IblOwner();
    const auto before = owner.GetTimingSampleCount();
    for (unsigned edit = 0U; edit < 16U; ++edit) {
      fog.SetFogInscatteringLuminance(glm::vec3(static_cast<float>(edit + 2U)));
      ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
      EXPECT_FALSE(Diagnostics().IsGpuTimelineEnabled());
    }
    ASSERT_GT(owner.GetTimingSampleCount(), before);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(12U));
    const auto settled = owner.GetTimingSampleCount();
    auto& profiler
      = RendererPublicationProbe::GetGpuTimelineProfiler(*renderer_);
    const auto final_timing = profiler.GetLastPublishedFrame();
    ASSERT_HAS_VALUE(final_timing);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(6U));
    EXPECT_EQ(owner.GetTimingSampleCount(), settled);
    const auto last_frame = profiler.GetLastPublishedFrame();
    ASSERT_HAS_VALUE(last_frame);
    EXPECT_EQ(last_frame->frame_sequence, final_timing->frame_sequence);
    EXPECT_FALSE(Diagnostics().IsGpuTimelineEnabled());
    RecordProperty("ibl_timing_samples", settled);
  }

#if defined(_MSC_VER) && defined(_DEBUG)
  NOLINT_TEST_F(IblSurfaceGpuTest, SourceResolutionAllocationHonorsUpdatePolicy)
  {
    namespace env = environment::internal;
    using graphics::testing::HeapAllocationFailure;
    constexpr unsigned kFrameSlots = 3U;
    const auto cube = AddUniformCube({ 2, 2, 2, 1 });
    Sky().SetCubemapResource(cube);
    Material(MaterialDomain::kOpaque, kRoughDielectric);
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    const auto binder
      = RendererPublicationProbe::SkyTextureBinder(*SceneRendererOf());
    ASSERT_TRUE(binder);
    {
      auto resident = binder->AcquireReadyTexture(cube);
      ASSERT_TRUE(resident);
      const auto weak = std::weak_ptr(resident);
      resident.reset();
      ASSERT_TRUE(weak.expired());
    }
    for (const auto [replacement, authoring] : {
           std::pair { false, false },
           std::pair { false, true },
           std::pair { true, false },
           std::pair { true, true },
         }) {
      SCOPED_TRACE(::testing::Message()
        << "replacement=" << replacement << " authoring=" << authoring);
      auto processor = env::IblProcessor(*renderer_);
      auto state = env::StableAtmosphereState {};
      auto& sky_light = state.view_products.sky_light;
      sky_light.enabled = true;
      sky_light.lower_hemisphere_is_solid_color = false;
      if (!replacement) {
        sky_light.source = environment::kSkyLightSourceSpecifiedCubemap;
        sky_light.cubemap_resource = cube;
      }
      auto context = RenderContext {};
      context.scene = observer_ptr { scene.get() };
      const auto use_frame = [&](const unsigned number) -> void {
        context.frame_sequence = frame::SequenceNumber { number };
        context.frame_slot = frame::Slot { (number - 1U) % kFrameSlots };
      };
      const auto refresh = [&] -> env::IblProcessor::RefreshState {
        return processor.RefreshSkyLightProducts(
          {}, context, state, {}, binder);
      };
      use_frame(1U);
      ASSERT_TRUE(refresh().refreshed);
      WaitForQueueIdle();
      const auto original = processor.GetPublishedProducts();
      unsigned failure_frame = 2U;
      if (!replacement && !authoring) {
        use_frame(failure_frame);
        env::IblProcessor::RefreshState same_source_failure;
        {
          HeapAllocationFailure denied;
          same_source_failure = refresh();
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
      sky_light.source = environment::kSkyLightSourceSpecifiedCubemap;
      sky_light.cubemap_resource = cube;
      sky_light.source_cubemap_angle_radians = 0.25F;
      state.authoring_revision = authoring ? 1U : 0U;
      use_frame(failure_frame);
      env::IblProcessor::RefreshState rejected;
      const auto failures = HeapAllocationFailure::RejectedCount();
      {
        // GetOrAllocate hits the resident cache; AcquireReadyTexture must
        // allocate its lease before update policy is fully classified.
        HeapAllocationFailure denied;
        rejected = refresh();
      }
      EXPECT_GT(HeapAllocationFailure::RejectedCount(), failures);
      const bool retain_prior = !replacement && !authoring;
      EXPECT_EQ(rejected.probe_state.valid, retain_prior);
      EXPECT_EQ(rejected.probe_state.static_sky_light.unavailable_reason,
        environment::StaticSkyLightUnavailableReason::kProcessingFailed);
      EXPECT_EQ(
        processor.GetPublishedProducts(), retain_prior ? original : nullptr);
      bool refreshed = false;
      const auto retries = retain_prior ? 4U : 1U;
      for (unsigned retry = 1U; retry <= retries; ++retry) {
        use_frame(failure_frame + retry);
        const auto result = refresh();
        EXPECT_TRUE(result.probe_state.valid);
        refreshed = refreshed || result.refreshed;
      }
      EXPECT_TRUE(refreshed);
      WaitForQueueIdle();
    }
  }
#endif

  NOLINT_TEST_F(
    IblSurfaceGpuTest, ViewRecreationPreservesSharedIblAndRetainedCapture)
  {
    Sky().SetCubemapResource(AddUniformCube({ 2, 1, 0.5F, 1 }));
    Sky().SetSpecularIntensity(0.0F);
    Material(MaterialDomain::kOpaque, kRoughDielectric);
    TrackCaptureAdmission();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred());
    probe->draws = 0U;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(false));
    ASSERT_TRUE(admitted_);
    const auto retained = *admitted_;
    const auto id = admitted_view_;
    ASSERT_TRUE(renderer_->AcquireIblCapture(id));

    renderer_->RemovePublishedRuntimeView(frame, ViewId { surface_view_id });
    const auto removed = renderer_->AcquireIblCapture(id);
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error(), IblCaptureError::kUnavailable);

    admitted_ = Err(IblCaptureError::kUnavailable);
    probe->draws = 0U;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(false));
    ASSERT_TRUE(admitted_);
    ASSERT_TRUE(renderer_->AcquireIblCapture(admitted_view_));
    EXPECT_EQ(admitted_->Revision(), retained.Revision());
    EXPECT_EQ(admitted_->ProcessedCube(), retained.ProcessedCube());
    EXPECT_NEAR(SurfaceColor()[0], 2.0F, 0.0002F);
    const auto read = ReadRetained(retained);
    EXPECT_EQ(read.metadata.product_revision, retained.Revision());
    EXPECT_EQ(read.metadata.processing_flags, environment::kIblProductReady);
    RecordProperty("recreated_view_retained_revision", retained.Revision());
  }

  NOLINT_TEST_F(IblSurfaceGpuTest,
    RendererCaptureAdmissionPreservesLiveUpdatesAndRetainedReads)
  {
    const auto unavailable
      = renderer_->AcquireIblCapture(ViewId { surface_view_id });
    ASSERT_FALSE(unavailable);
    EXPECT_EQ(unavailable.error(), IblCaptureError::kUnavailable);
    Sky().SetSpecularIntensity(0.0F);
    Material(MaterialDomain::kOpaque, kRoughDielectric);
    TrackCaptureAdmission();
    const auto publish = [&](const float value) -> void {
      Sky().SetCubemapResource(AddUniformCube({ value, value, value, 1 }));
      RenderDeferred();
    };
    ASSERT_NO_FATAL_FAILURE(publish(1.0F));
    ASSERT_TRUE(admitted_);
    auto first = std::move(*admitted_);
    ASSERT_NO_FATAL_FAILURE(publish(2.0F));
    ASSERT_TRUE(admitted_);
    auto second = std::move(*admitted_);
    EXPECT_GT(second.Revision(), first.Revision());

    // Two retained captures exhaust admission; live updates continue.
    ASSERT_NO_FATAL_FAILURE(publish(3.0F));
    ASSERT_FALSE(admitted_);
    EXPECT_EQ(admitted_.error(), IblCaptureError::kBusy);
    EXPECT_NEAR(SurfaceColor()[0], 3.0F, 0.003F);
    first = {};
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    ASSERT_NO_FATAL_FAILURE(RenderDeferred(1U));
    ASSERT_TRUE(admitted_);
    const auto third = std::move(*admitted_);
    EXPECT_GT(third.Revision(), second.Revision());

    probe->inspect = {};
    const auto capture = BeginOptionalCapture();
    renderer_->OnShutdown();
    EXPECT_FALSE(renderer_->AcquireIblCapture(ViewId { surface_view_id }));
    const auto read = ReadRetained(second);
    EXPECT_EQ(read.first_texel, (Pixel { 2, 2, 2, 1 }));
    EXPECT_EQ(read.metadata.product_revision, second.Revision());
    EXPECT_EQ(read.metadata.processing_flags, environment::kIblProductReady);
    if (capture) {
      EXPECT_TRUE(capture->EndCapture());
    }
    RecordProperty("retained_capture_revision", second.Revision());
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, GainOnlyEditsReachNextFrameWithoutRecapture)
  {
    constexpr Pixel radiance { 0.25F, 0.5F, 1.0F, 1.0F };
    Sky().SetCubemapResource(AddUniformCube(radiance));
    std::uint32_t revision = 0;
    probe->inspect = [&](const RenderContext& /*context*/,
                       const SceneTextureExtractRef& /*scene_textures*/,
                       unsigned /*draws*/) -> void {
      revision
        = SceneRendererOf()->GetLastEnvironmentLightingState().probe_revision;
    };
    constexpr glm::vec3 kWarmTint { 0.5F, 0.25F, 0.125F };
    const std::array controls {
      SkyControls {},
      SkyControls { .intensity = 0 },
      SkyControls { .intensity = 3 },
      SkyControls { .intensity = 3, .diffuse = 0 },
      SkyControls { .intensity = 3, .diffuse = 0.25F },
      SkyControls { .intensity = 3, .diffuse = 0.25F, .tint = kWarmTint },
      SkyControls {
        .intensity = 3,
        .diffuse = 0.25F,
        .specular = 0,
        .tint = kWarmTint,
      },
      SkyControls {
        .intensity = 3,
        .diffuse = 0.25F,
        .reflections = false,
        .tint = kWarmTint,
      },
    };
    unsigned cases = 0;
    for (const bool forward : { false, true }) {
      for (const auto domain : kOpaqueAndBlended) {
        const bool deferred_shading
          = !forward && domain != MaterialDomain::kAlphaBlended;
        CapturePackedInputs(deferred_shading);
        Material(domain, kRoughDielectric);
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F));
        const auto expected_revision = revision;
        ASSERT_GT(expected_revision, 0U);
        // A white rough dielectric; deferred shading reads its specular from
        // the G-buffer.
        auto surface = SurfaceInputs {};
        if (deferred_shading) {
          surface.specular = PackedSurfaceInputs().material[1];
        }
        for (const auto& control : controls) {
          ApplySky(control);
          SCOPED_TRACE(::testing::Message()
            << "case=" << cases << " forward=" << forward
            << " domain=" << to_string(domain));
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
          EXPECT_EQ(revision, expected_revision);
          const auto pixel = SurfaceColor();
          for (unsigned channel = 0; channel < 3; ++channel) {
            const double expected
              = SplitSum(radiance, surface, control, channel)
              * Coverage(domain);
            EXPECT_NEAR(pixel[channel], expected, (expected * 0.001) + 1.0e-6);
          }
          ++cases;
        }
      }
    }
    RecordProperty("ibl_immediate_gain_cases", cases);
  }

  NOLINT_TEST_F(IblSurfaceGpuTest, ImagesAgreeAcrossPublishedAndOffscreenPaths)
  {
    constexpr std::uint32_t width = 128U;
    constexpr std::uint32_t height = 96U;
    // Display budgets for different G-buffer inputs, in 8-bit code
    // equivalents; FP16 filtering is checked separately.
    constexpr double kDisplayPeakCodes = 4.0;
    constexpr double kDisplayRmsCodes = 1.0;
    ASSERT_NO_FATAL_FAILURE(UseImageView(width, height));
    Diagnostics().SetHdrPrecisionControl(HdrPrecisionControl::kProduction);
    TrackPublishedProducts();
    const auto dump = ImageDump {};
    auto records = nlohmann::json::array();
    auto packed_references = nlohmann::json::array();
    double maximum_relative = 0;
    auto maximum_display = CodeDifference { .peak = 0, .rms = 0 };
    unsigned cases = 0;
    // Each source adds to the previous one's environment.
    for (unsigned source = 0; source < 3; ++source) {
      auto ev = 0.0F;
      if (source == 0) {
        Sky().SetCubemapResource(AddCube(32U,
          [](const std::uint32_t face, const std::uint32_t x,
            const std::uint32_t y) -> Pixel {
            const auto direction
              = WorldDirection({ .face = face, .x = x, .y = y, .size = 32U });
            return Pixel {
              .5F + (.1F * direction.x),
              .5F + (.1F * direction.y),
              .5F + (.1F * direction.z),
              1,
            };
          }));
      } else if (source == 1) {
        UseCapturedFogIrradiance({ .25F, .5F, 1.0F });
        ev = 1.0F;
      } else {
        auto& atmosphere = scene->GetEnvironment()
                             ->AddSystem<scene::environment::SkyAtmosphere>();
        atmosphere.SetEnabled(true);
        atmosphere.SetRenderInMainPass(false);
        const auto fog = scene->GetEnvironment()->TryGetSystem<Fog>();
        ASSERT_TRUE(fog);
        fog->SetFogDensity(.0001F);
        fog->SetHeightFalloffPerMeter(.001F);
        ASSERT_NO_FATAL_FAILURE(AddPrimarySun(100000.0F));
        ev = 9.0F;
      }
      Sky().SetDiffuseIntensity(1);
      Sky().SetSpecularIntensity(1);
      scene->NotifyEnvironmentAuthoringChange();
      for (const float metallic : { 0.0F, 1.0F }) {
        for (const float roughness : { .2F, .6F, 1.0F }) {
          const auto capture = source == 2 && metallic == 1 && roughness == .6F
            ? BeginOptionalCapture()
            : observer_ptr<graphics::FrameCaptureController> {};
          const auto finish_capture = ScopeGuard([&] noexcept -> void {
            if (capture) {
              EXPECT_TRUE(capture->EndCapture());
            }
          });
          const auto prefix = std::to_string(source) + "-"
            + std::to_string(static_cast<int>(metallic)) + "-"
            + std::to_string(roughness);
          // Deferred opaque shading through the runtime publication is the
          // reference of every other path.
          std::vector<Pixel> reference;
          std::vector<Pixel> forward_reference;
          std::array<std::vector<Pixel>, 2> deferred_display;
          GBufferTexels packed {};
          for (const bool translucent : { false, true }) {
            for (const bool forward : { false, true }) {
              std::vector<Pixel> runtime_display;
              for (const bool published : { true, false }) {
                const auto name = prefix + "-"
                  + (translucent ? "alpha" : "opaque") + "-"
                  + (forward ? "forward" : "deferred") + "-"
                  + (published ? "runtime" : "offscreen");
                SCOPED_TRACE(name);
                CapturePackedInputs(!forward && !translucent);
                Material(translucent ? MaterialDomain::kAlphaBlended
                                     : MaterialDomain::kOpaque,
                  { .roughness = roughness, .metallic = metallic });
                SkyContribution image;
                ASSERT_NO_FATAL_FAILURE(
                  RenderSkyContribution(published, forward, ev, image));
                ASSERT_EQ(image.lit.size(), width * height);
                const auto coverage = translucent ? .5F : 1.0F;
                auto contribution = image.Difference();
                for (std::size_t i = 0; i < contribution.size(); ++i) {
                  ASSERT_EQ(image.lit[i][3], coverage);
                  for (unsigned c = 0; c < 3; ++c) {
                    contribution[i][c] /= coverage;
                    ASSERT_TRUE(std::isfinite(contribution[i][c]));
                    ASSERT_GT(contribution[i][c], 0);
                  }
                  contribution[i][3] = 1;
                }
                double peak_error = 0;
                if (reference.empty()) {
                  reference = contribution;
                  packed = PackedSurfaceInputs();
                  EXPECT_EQ(packed.base_color, (Pixel { 1, 1, 1, 1 }));
                  EXPECT_FLOAT_EQ(packed.material[0], metallic);
                  EXPECT_NEAR(packed.material[2], roughness, 1.0e-6F);
                } else {
                  peak_error = MaximumRelativeError(contribution, reference);
                }
                if (forward || translucent) {
                  if (forward_reference.empty()) {
                    forward_reference = contribution;
                  } else {
                    EXPECT_EQ(contribution, forward_reference);
                  }
                } else {
                  EXPECT_EQ(contribution, reference);
                }
                maximum_relative = std::max(maximum_relative, peak_error);

                const auto display = DisplayImage();
                ASSERT_EQ(display.size(), width * height);
                for (const auto& pixel : display) {
                  for (unsigned channel = 0; channel < 3; ++channel) {
                    ASSERT_TRUE(std::isfinite(pixel[channel]));
                    ASSERT_GT(pixel[channel], 0);
                    ASSERT_LT(pixel[channel], 1);
                  }
                }
                auto difference = CodeDifference { .peak = 0, .rms = 0 };
                auto& deferred = deferred_display.at(translucent ? 1 : 0);
                if (!forward) {
                  deferred = display;
                } else {
                  ASSERT_EQ(deferred.size(), display.size());
                  difference = DisplayDifference(display, deferred);
                  EXPECT_LE(difference.peak, kDisplayPeakCodes);
                  EXPECT_LE(difference.rms, kDisplayRmsCodes);
                  maximum_display.peak
                    = std::max(maximum_display.peak, difference.peak);
                  maximum_display.rms
                    = std::max(maximum_display.rms, difference.rms);
                }
                if (published) {
                  runtime_display = display;
                } else {
                  EXPECT_EQ(display, runtime_display);
                }
                ASSERT_NO_FATAL_FAILURE(
                  dump.Save(name + "-baseline", image.baseline));
                ASSERT_NO_FATAL_FAILURE(dump.Save(name + "-lit", image.lit));
                ASSERT_NO_FATAL_FAILURE(dump.Save(name + "-display", display));
                records.push_back({
                  { "name", name },
                  { "coverage", coverage },
                  { "exposure_ev", ev },
                  { "maximum_relative_error", peak_error },
                  { "display_peak_code_equivalents", difference.peak },
                  { "display_rms_code_equivalents", difference.rms },
                });
                ++cases;
              }
            }
          }

          // Compare transport using the actual deferred shading normal. The
          // images above retain the UNORM10 normal-packing difference.
          // UNORM10 octahedral lanes centre on code 511 (see GBufferHelpers).
          const auto unpack = [](const float stored) -> float {
            return (std::round(stored * 1023.0F) - 511.0F) / 511.0F;
          };
          const float nx = unpack(packed.normal[0]);
          const float ny = unpack(packed.normal[1]);
          const auto normal = glm::normalize(
            glm::vec3(nx, ny, 1 - std::abs(nx) - std::abs(ny)));
          ASSERT_GT(normal.z, 0);
          const auto normal_texture = AddTexel({
            .5F + (.5F * normal.x),
            .5F + (.5F * normal.y),
            .5F + (.5F * normal.z),
            1,
          });
          CapturePackedInputs(false);
          Material(MaterialDomain::kOpaque,
            { .roughness = roughness, .metallic = metallic }, kWhite,
            normal_texture);
          SkyContribution matched;
          ASSERT_NO_FATAL_FAILURE(
            RenderSkyContribution(false, true, ev, matched));
          ASSERT_EQ(matched.lit.size(), reference.size());
          const auto matched_contribution = matched.Difference();
          for (const auto& pixel : matched_contribution) {
            for (unsigned c = 0; c < 3; ++c) {
              ASSERT_TRUE(std::isfinite(pixel[c]));
              ASSERT_GT(pixel[c], 0);
            }
          }
          const auto maximum_residual
            = MaximumRelativeError(matched_contribution, reference);
          const auto f0_bound = metallic == 0
            ? std::abs((.08 * packed.material[1]) - .04) / .04
            : 0.0;

          // Repeat both paths with the canonical FP32 products of the same
          // generation; the differences are the half-precision filtering.
          ASSERT_NE(published_, nullptr);
          const auto comparison_products = published_;
          const auto metadata = ReadMetadata(*comparison_products);
          auto canonical_metadata = metadata;
          canonical_metadata.precision_flags = 0;
          OverwriteMetadata(*comparison_products, canonical_metadata);
          const auto restore_metadata = ScopeGuard([&] noexcept -> void {
            OverwriteMetadata(*comparison_products, metadata);
          });
          const auto canonical
            = [&](const bool forward, const content::ResourceKey normal_key,
                std::vector<Pixel>& result) -> void {
            Material(MaterialDomain::kOpaque,
              { .roughness = roughness, .metallic = metallic }, kWhite,
              normal_key);
            SkyContribution image;
            ASSERT_NO_FATAL_FAILURE(
              RenderSkyContribution(false, forward, ev, image));
            ASSERT_EQ(published_.get(), comparison_products.get());
            result = image.Difference();
          };
          std::vector<Pixel> canonical_deferred;
          std::vector<Pixel> canonical_forward;
          ASSERT_NO_FATAL_FAILURE(canonical(false, {}, canonical_deferred));
          ASSERT_NO_FATAL_FAILURE(
            canonical(true, normal_texture, canonical_forward));
          ASSERT_EQ(canonical_deferred.size(), reference.size());
          ASSERT_EQ(canonical_forward.size(), reference.size());
          double canonical_residual = 0;
          double filter_rgb = 0;
          double filter_ev = 0;
          for (std::size_t i = 0; i < reference.size(); ++i) {
            for (unsigned c = 0; c < 3; ++c) {
              const double d = canonical_deferred[i][c];
              const double f = canonical_forward[i][c];
              ASSERT_TRUE(std::isfinite(d) && std::isfinite(f));
              ASSERT_GT(d, 0);
              ASSERT_GT(f, 0);
              canonical_residual
                = std::max(canonical_residual, std::abs(d - f) / d);
              for (const auto& [half, full] :
                {
                  std::pair<double, double> { reference[i][c], d },
                  std::pair<double, double> { matched_contribution[i][c], f },
                }) {
                filter_rgb = std::max(filter_rgb, std::abs(half - full) / full);
                filter_ev
                  = std::max(filter_ev, std::abs(std::log2(half / full)));
              }
            }
          }
          // The normal/F0-matched residual is diagnostic evidence only. Its
          // small transport/sampling differences are not a filtering budget.
          EXPECT_LE(filter_rgb, .0025);
          EXPECT_LE(filter_ev, 2.0 / 1024.0);
          const auto name = prefix + "-packed-normal-reference";
          ASSERT_NO_FATAL_FAILURE(
            dump.Save(name + "-baseline", matched.baseline));
          ASSERT_NO_FATAL_FAILURE(dump.Save(name + "-lit", matched.lit));
          ASSERT_NO_FATAL_FAILURE(
            dump.Save(name + "-canonical-deferred", canonical_deferred));
          ASSERT_NO_FATAL_FAILURE(
            dump.Save(name + "-canonical-forward", canonical_forward));
          packed_references.push_back({
            { "name", name },
            {
              "packed_inputs",
              { packed.base_color, packed.material, packed.normal },
            },
            { "normal", { normal.x, normal.y, normal.z } },
            { "f0_relative_bound", f0_bound },
            { "maximum_residual", maximum_residual },
            { "canonical_residual", canonical_residual },
            { "filter_relative_rgb", filter_rgb },
            { "filter_stops", filter_ev },
          });
        }
      }
    }
    ASSERT_NO_FATAL_FAILURE(dump.WriteManifest({
      { "width", width },
      { "height", height },
      { "cases", records },
      { "packed_normal_references", packed_references },
      { "maximum_relative_error", maximum_relative },
      { "maximum_display_peak_code_equivalents", maximum_display.peak },
      { "maximum_display_rms_code_equivalents", maximum_display.rms },
      { "display_limits",
        {
          { "peak_code_equivalents", kDisplayPeakCodes },
          { "rms_code_equivalents", kDisplayRmsCodes },
        } },
    }));
    RecordProperty("image_cases", cases);
    RecordProperty("maximum_image_relative_error", maximum_relative);
  }
} // namespace
} // namespace oxygen::vortex::testing::exposure
