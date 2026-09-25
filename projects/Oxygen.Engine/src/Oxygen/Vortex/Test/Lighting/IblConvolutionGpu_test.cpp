//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/StaticSkyLightProcessor.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::testing {
namespace {
  using Pixel = std::array<float, 4>;
  using environment::internal::IblGpuProcessor;
  using environment::internal::IblGpuProducts;
  using environment::internal::IblProcessSettings;
  using graphics::ResourceStates;

  struct SurfaceInput {
    GpuSkyLightParams light;
    std::array<float, 3> normal { 0.0F, 0.0F, 1.0F };
    float roughness { 0.5F };
    std::array<float, 3> view { 0.0F, 0.0F, 1.0F };
    float metallic { 0.0F };
    std::array<float, 3> base_color { 0.8F, 0.2F, 0.1F };
    float occlusion { 1.0F };
    std::array<float, 3> f0 { 0.04F, 0.04F, 0.04F };
    std::uint32_t brdf_srv { kInvalidBindlessIndex };
  };
  static_assert(sizeof(SurfaceInput) == 144U);

  auto Surface(const IblGpuProducts& products) -> SurfaceInput
  {
    auto result = SurfaceInput {};
    result.light.enabled = 1U;
    result.light.product_metadata_srv = products.metadata_srv.get();
    result.light.diffuse_sh_slot = products.diffuse_sh_srv.get();
    result.light.prefilter_map_slot = products.specular_srv.get();
    result.light.prefilter_max_mip = products.maximum_mip;
    result.light.ibl_generation = products.revision;
    result.brdf_srv = products.brdf->srv.get();
    return result;
  }

  // Independent face basis in hardware cube space, converted to Oxygen +Z up.
  auto Direction(std::uint32_t face, std::uint32_t x, std::uint32_t y,
    std::uint32_t size) -> glm::vec3
  {
    constexpr auto centers = std::array { glm::vec3 { 1, 0, 0 },
      glm::vec3 { -1, 0, 0 }, glm::vec3 { 0, 1, 0 }, glm::vec3 { 0, -1, 0 },
      glm::vec3 { 0, 0, 1 }, glm::vec3 { 0, 0, -1 } };
    constexpr auto rights = std::array { glm::vec3 { 0, 0, -1 },
      glm::vec3 { 0, 0, 1 }, glm::vec3 { 1, 0, 0 }, glm::vec3 { 1, 0, 0 },
      glm::vec3 { 1, 0, 0 }, glm::vec3 { -1, 0, 0 } };
    constexpr auto downs = std::array { glm::vec3 { 0, -1, 0 },
      glm::vec3 { 0, -1, 0 }, glm::vec3 { 0, 0, 1 }, glm::vec3 { 0, 0, -1 },
      glm::vec3 { 0, -1, 0 }, glm::vec3 { 0, -1, 0 } };
    const auto u = 2.0F * (x + 0.5F) / size - 1.0F;
    const auto v = 2.0F * (y + 0.5F) / size - 1.0F;
    const auto p = centers[face] + u * rights[face] + v * downs[face];
    const auto inverse_length
      = 1.0F / std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    return glm::vec3 { p.x, -p.z, p.y } * inverse_length;
  }

  struct ReferenceSample {
    std::array<float, 3> direction;
    float lod;
    std::uint32_t source;
    std::array<std::uint32_t, 3> padding {};
  };
  static_assert(sizeof(ReferenceSample) == 32U);

  struct ReferencePlan {
    std::vector<ReferenceSample> samples;
    std::vector<double> weights;
  };

  // Double-precision CPU integration chooses directions, PDFs, footprint LODs
  // and weights independently of the production HLSL. The GPU probe supplies
  // only native seamless TextureCube sampling from the common source mips.
  auto PrefilterReferencePlan(glm::dvec3 normal, double roughness,
    std::uint32_t size, std::uint32_t maximum_mip, std::uint32_t count,
    ShaderVisibleIndex source) -> ReferencePlan
  {
    auto result = ReferencePlan {};
    const auto tangent = glm::normalize(
      glm::cross(std::abs(normal.z) < 0.999 ? glm::dvec3 { 0, 0, 1 }
                                            : glm::dvec3 { 1, 0, 0 },
        normal));
    const auto bitangent = glm::cross(normal, tangent);
    const auto texel_angle = 8.0 * std::numbers::pi / (6.0 * size * size);
    for (auto sample = 0U; sample < count; ++sample) {
      auto inverse = 0.0;
      auto factor = 0.5;
      for (auto bits = sample; bits != 0U; bits >>= 1U) {
        inverse += (bits & 1U) * factor;
        factor *= 0.5;
      }
      const auto phi = 2.0 * std::numbers::pi * sample / count;
      auto direction = glm::dvec3 {};
      double pdf;
      double weight;
      if (roughness > 0.99) {
        const auto cosine = std::sqrt(inverse);
        const auto sine = std::sqrt(1.0 - inverse);
        direction = { sine * std::cos(phi), sine * std::sin(phi), cosine };
        pdf = cosine / std::numbers::pi;
        weight = 1.0;
      } else {
        const auto alpha = roughness * roughness;
        const auto u = inverse * 0.995;
        const auto theta = std::atan(alpha * std::sqrt(u / (1.0 - u)));
        const auto nh = std::cos(theta);
        const auto sine = std::sin(theta);
        const auto h
          = glm::dvec3 { sine * std::cos(phi), sine * std::sin(phi), nh };
        direction = 2.0 * nh * h - glm::dvec3 { 0, 0, 1 };
        if (direction.z <= 0.0)
          continue;
        const auto denominator = sine * sine + alpha * alpha * nh * nh;
        pdf = alpha * alpha
          / (4.0 * std::numbers::pi * denominator * denominator);
        weight = direction.z;
      }
      const auto lod = pdf > 0.0
        ? std::clamp(std::log2(1.0 / (count * pdf * texel_angle)) / 2.0, 0.0,
            double(maximum_mip))
        : double(maximum_mip);
      const auto world = tangent * direction.x + bitangent * direction.y
        + normal * direction.z;
      result.samples.push_back(
        { { float(world.x), float(world.y), float(world.z) }, float(lod),
          source.get() });
      result.weights.push_back(weight);
    }
    return result;
  }

  class IblConvolutionGpuTest : public LightingGpuAbiTest {
  protected:
    static auto CapturePath() -> std::string
    {
      char* value = nullptr;
      std::size_t size = 0U;
      if (_dupenv_s(&value, &size, "OXYGEN_IBL_CAPTURE") != 0
        || value == nullptr)
        return {};
      const auto owned
        = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
      return owned.get();
    }

    auto BackendConfigJson() const -> std::string override
    {
      return CapturePath().empty()
        ? LightingGpuAbiTest::BackendConfigJson()
        : R"({"enable_debug_layer":true,"frame_capture":{"provider":"renderdoc","init_mode":"search"}})";
    }

    auto BeginCapture() -> void
    {
      const auto path = CapturePath();
      if (path.empty())
        return;
      WaitForQueueIdle();
      const auto capture = Backend().GetFrameCaptureController();
      CHECK_F(capture && capture->IsAvailable());
      CHECK_F(capture->SetCaptureFileTemplate(path));
      CHECK_F(capture->StartCapture());
    }

    auto EndCapture() -> void
    {
      if (const auto capture = Backend().GetFrameCaptureController();
        capture && capture->IsCapturing())
        CHECK_F(capture->EndCapture());
    }

    auto TearDown() -> void override
    {
      EndCapture();
      processor_.reset();
      brdf_resources_.reset();
      LightingGpuAbiTest::TearDown();
    }

    std::unique_ptr<environment::internal::IblBrdfResources> brdf_resources_;
    std::unique_ptr<IblGpuProcessor> processor_;
    auto Processor() -> IblGpuProcessor&
    {
      if (!processor_)
        processor_ = std::make_unique<IblGpuProcessor>(Backend());
      return *processor_;
    }

    auto PrepareBrdf()
      -> std::shared_ptr<const environment::internal::IblBrdfProduct>
    {
      if (!brdf_resources_)
        brdf_resources_
          = std::make_unique<environment::internal::IblBrdfResources>(
            Backend());
      auto prepared = brdf_resources_->Prepare();
      CHECK_F(prepared.has_value());
      return *prepared;
    }

    struct Source {
      std::shared_ptr<graphics::Texture> texture;
      graphics::RegistrationLease registration;
    };

    auto Shade(const IblGpuProducts& products,
      std::span<const SurfaceInput> inputs) -> std::vector<std::array<float, 6>>
    {
      const auto words = Decode({ .records = std::as_bytes(inputs),
        .stride = sizeof(SurfaceInput),
        .record_kind = 32U,
        .decoded_words = 6U,
        .count = static_cast<std::uint32_t>(inputs.size()),
        .prepare = [&](graphics::CommandRecorder& recorder) {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
        } });
      auto result = std::vector<std::array<float, 6>>(inputs.size());
      std::memcpy(
        result.data(), words.data(), words.size() * sizeof(std::uint32_t));
      return result;
    }

    auto Reference(const IblGpuProducts& products, const ReferencePlan& plan)
      -> glm::dvec3
    {
      const auto samples
        = Decode({ .records = std::as_bytes(std::span(plan.samples)),
          .stride = sizeof(ReferenceSample),
          .record_kind = 33U,
          .decoded_words = 3U,
          .count = static_cast<std::uint32_t>(plan.samples.size()),
          .prepare = [&](graphics::CommandRecorder& recorder) {
            CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
          } });
      auto sum = glm::dvec3 {};
      auto total = 0.0;
      for (auto i = 0U; i < plan.weights.size(); ++i) {
        for (auto c = 0U; c < 3U; ++c)
          sum[c] += std::bit_cast<float>(samples[i * 3U + c]) * plan.weights[i];
        total += plan.weights[i];
      }
      return sum / total;
    }

    auto MakeSource(std::uint32_t size,
      const std::function<Pixel(std::uint32_t, std::uint32_t, std::uint32_t)>&
        texel) -> Source
    {
      auto& graphics = Backend();
      auto texture = graphics.CreateTexture({ .width = size,
        .height = size,
        .array_size = 6U,
        .format = Format::kRGBA32Float,
        .texture_type = TextureType::kTextureCube,
        .debug_name = "IBL.ReferenceSource",
        .is_shader_resource = true,
        .initial_state = ResourceStates::kCommon });
      auto registration
        = graphics.GetResourceRegistry().RegisterManaged(texture);
      CHECK_F(registration.has_value());
      auto subresources = std::array<upload::UploadSubresource, 6> {};
      for (auto face = 0U; face < 6U; ++face)
        subresources[face].array_slice = face;
      const auto plan
        = upload::UploadPlanner::PlanTexture2D({ .dst = texture }, subresources,
          upload::UploadPolicy {
            graphics.QueueKeyFor(graphics::QueueRole::kGraphics) });
      CHECK_F(plan.has_value());
      auto staging = CreateRegisteredBuffer({ .size_bytes = plan->total_bytes,
        .memory = graphics::BufferMemory::kUpload,
        .debug_name = "IBL.ReferenceUpload" });
      auto row = std::vector<Pixel>(size);
      for (auto face = 0U; face < 6U; ++face) {
        const auto& region = plan->regions[face];
        for (auto y = 0U; y < size; ++y) {
          for (auto x = 0U; x < size; ++x)
            row[x] = texel(face, x, y);
          staging->Update(row.data(), row.size() * sizeof(Pixel),
            region.buffer_offset
              + static_cast<std::uint64_t>(y) * region.buffer_row_pitch);
        }
      }
      SubmitCommands(
        "IBL reference upload", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(static_cast<bool>(recorder.RetainRegistration(
            graphics.GetResourceRegistry(), *registration)));
          EnsureTracked(recorder, staging, ResourceStates::kGenericRead);
          recorder.BeginTrackingResourceState(
            *texture, ResourceStates::kCommon);
          recorder.RequireResourceState(*texture, ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          for (const auto& region : plan->regions)
            recorder.CopyBufferToTexture(*staging, region, *texture);
          recorder.RequireResourceStateFinal(
            *texture, ResourceStates::kShaderResource);
        });
      return { texture, std::move(*registration) };
    }

    template <typename Value>
    auto ReadBuffer(
      const IblGpuProducts& products, const graphics::Buffer& buffer) -> Value
    {
      auto readback
        = GetReadbackManager()->CreateBufferReadback("IBL product check");
      SubmitCommands(
        "IBL buffer readback", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
          recorder.FlushBarriers();
          CHECK_F(readback->EnqueueCopy(recorder, buffer, { 0U, sizeof(Value) })
              .has_value());
        });
      auto mapped = readback->MapNow();
      CHECK_F(mapped.has_value());
      auto value = Value {};
      std::memcpy(&value, mapped->Bytes().data(), sizeof(Value));
      return value;
    }

    auto ReadFace(const IblGpuProducts& products,
      const graphics::Texture& texture, std::uint32_t face, std::uint32_t mip)
      -> std::vector<Pixel>
    {
      const auto size = texture.GetDescriptor().width >> mip;
      auto readback
        = GetReadbackManager()->CreateTextureReadback("IBL face check");
      SubmitCommands(
        "IBL face readback", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
          recorder.FlushBarriers();
          CHECK_F(readback
              ->EnqueueCopy(recorder, texture,
                { .src_slice = { .width = size,
                    .height = size,
                    .depth = 1U,
                    .mip_level = mip,
                    .array_slice = face } })
              .has_value());
        });
      auto mapped = readback->MapNow();
      CHECK_F(mapped.has_value());
      auto pixels = std::vector<Pixel>(size * size);
      for (auto y = 0U; y < size; ++y) {
        for (auto x = 0U; x < size; ++x) {
          auto half = std::array<std::uint16_t, 4> {};
          std::memcpy(half.data(),
            mapped->Data() + y * mapped->Layout().row_pitch.get() + x * 8U, 8U);
          for (auto channel = 0U; channel < 4U; ++channel)
            pixels[y * size + x][channel]
              = data::HalfFloat { half[channel] }.ToFloat();
        }
      }
      return pixels;
    }
  };

  NOLINT_TEST_F(IblConvolutionGpuTest, ConstantCubePreservesEveryFaceAndMip)
  {
    const auto source = MakeSource(128U,
      [](auto, auto, auto) { return Pixel { 1.0F, 0.5F, 0.125F, 1.0F }; });
    BeginCapture();
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 128U, .lower_hemisphere_solid_color = false }, 31U);
    ASSERT_TRUE(products.has_value());
    EXPECT_TRUE((*products)->producer.IsValid());
    EndCapture();
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      **products, *(*products)->metadata);
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_EQ(metadata.product_revision, 31U);
    EXPECT_FLOAT_EQ(metadata.source_radiance_scale, 1.0F);
    EXPECT_NEAR(metadata.average_brightness, 1.625F / 3.0F, 0.0001F);
    const auto sh = ReadBuffer<std::array<glm::vec4, 8>>(
      **products, *(*products)->diffuse_sh);
    for (const auto n :
      { glm::vec3 { 1, 0, 0 }, glm::vec3 { 0, 1, 0 }, glm::vec3 { 0, 0, 1 } }) {
      const auto value
        = environment::internal::EvaluatePackedStaticSkyLightDiffuseSh(sh, n);
      EXPECT_NEAR(value.r, 1.0F, 0.001F);
      EXPECT_NEAR(value.g, 0.5F, 0.001F);
      EXPECT_NEAR(value.b, 0.125F, 0.001F);
    }
    for (auto mip = 0U; mip < 8U; ++mip) {
      for (auto face = 0U; face < 6U; ++face) {
        SCOPED_TRACE(::testing::Message() << "face " << face << " mip " << mip);
        const auto pixels
          = ReadFace(**products, *(*products)->specular_cube, face, mip);
        for (const auto& pixel : pixels) {
          ASSERT_NEAR(pixel[0], 1.0F, 0.001F);
          ASSERT_NEAR(pixel[1], 0.5F, 0.001F);
          ASSERT_NEAR(pixel[2], 0.125F, 0.001F);
          ASSERT_FLOAT_EQ(pixel[3], 1.0F);
        }
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, HdrRangeScaleIsAppliedOnceAfterConvolution)
  {
    const auto source = MakeSource(32U,
      [](auto, auto, auto) { return Pixel { 1.0e8F, 2.0e6F, 8.0F, 1.0F }; });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 32U, .lower_hemisphere_solid_color = false }, 32U);
    ASSERT_TRUE(products.has_value());
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      **products, *(*products)->metadata);
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_NEAR(metadata.source_radiance_scale, 1.0e8F / 65504.0F, 0.001F);
    const auto sh = ReadBuffer<std::array<glm::vec4, 8>>(
      **products, *(*products)->diffuse_sh);
    const auto diffuse
      = environment::internal::EvaluatePackedStaticSkyLightDiffuseSh(
          sh, { 0, 0, 1 })
      * metadata.source_radiance_scale;
    EXPECT_NEAR(diffuse.r, 1.0e8F, 1.0e5F);
    EXPECT_NEAR(diffuse.g, 2.0e6F, 2000.0F);
    EXPECT_NEAR(diffuse.b, 8.0F, 0.01F);
    const auto pixel
      = ReadFace(**products, *(*products)->specular_cube, 0U, 5U).front();
    EXPECT_NEAR(pixel[0] * metadata.source_radiance_scale, 1.0e8F, 1.0e5F);
    EXPECT_NEAR(pixel[1] * metadata.source_radiance_scale, 2.0e6F, 2000.0F);
    EXPECT_NEAR(pixel[2] * metadata.source_radiance_scale, 8.0F, 0.01F);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, DirectionalCubeMatchesAnalyticDiffuseInOxygenAxes)
  {
    const auto source = MakeSource(128U, [](auto face, auto x, auto y) {
      const auto n = Direction(face, x, y, 128U);
      return Pixel { 1.0F + 0.75F * n.x, 1.0F + 0.75F * n.y, 1.0F + 0.75F * n.z,
        1.0F };
    });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 128U, .lower_hemisphere_solid_color = false }, 33U);
    ASSERT_TRUE(products.has_value());
    const auto sh = ReadBuffer<std::array<glm::vec4, 8>>(
      **products, *(*products)->diffuse_sh);
    for (const auto n : { glm::vec3 { 1, 0, 0 }, glm::vec3 { -1, 0, 0 },
           glm::vec3 { 0, 1, 0 }, glm::vec3 { 0, -1, 0 }, glm::vec3 { 0, 0, 1 },
           glm::vec3 { 0, 0, -1 } }) {
      const auto diffuse
        = environment::internal::EvaluatePackedStaticSkyLightDiffuseSh(sh, n);
      EXPECT_NEAR(diffuse.r, 1.0F + 0.5F * n.x, 0.001F);
      EXPECT_NEAR(diffuse.g, 1.0F + 0.5F * n.y, 0.001F);
      EXPECT_NEAR(diffuse.b, 1.0F + 0.5F * n.z, 0.001F);
    }
    for (auto face = 0U; face < 6U; ++face) {
      const auto pixels
        = ReadFace(**products, *(*products)->processed_cube, face, 0U);
      for (const auto y : { 0U, 63U, 127U }) {
        for (const auto x : { 0U, 63U, 127U }) {
          const auto direction = Direction(face, x, y, 128U);
          for (auto channel = 0U; channel < 3U; ++channel)
            EXPECT_NEAR(pixels[y * 128U + x][channel],
              1.0F + 0.75F * direction[channel], 0.001F);
        }
      }
    }
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, InvalidSourceCannotPublishValidMetadata)
  {
    const auto source = MakeSource(16U, [](auto, auto, auto) {
      return Pixel { std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F,
        1.0F };
    });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 34U);
    ASSERT_TRUE(products.has_value());
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      **products, *(*products)->metadata);
    EXPECT_EQ(metadata.processing_flags, environment::kIblProductComplete);
    EXPECT_EQ(metadata.product_revision, 34U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, SourceMipsUseFourChildBoxReduction)
  {
    const auto source = MakeSource(16U, [](auto face, auto x, auto y) {
      return Pixel { 1.0F + static_cast<float>(x % 2U),
        2.0F + static_cast<float>(y % 2U), static_cast<float>(face) + 0.5F,
        1.0F };
    });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 37U);
    ASSERT_TRUE(products.has_value());
    for (auto face = 0U; face < 6U; ++face) {
      for (auto mip = 1U; mip < 5U; ++mip) {
        const auto pixels
          = ReadFace(**products, *(*products)->processed_cube, face, mip);
        for (const auto& pixel : pixels) {
          EXPECT_EQ(pixel,
            (Pixel { 1.5F, 2.5F, static_cast<float>(face) + 0.5F, 1.0F }));
        }
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, LowerHemisphereUsesWorldUpBeforeConvolution)
  {
    const auto source = MakeSource(
      32U, [](auto, auto, auto) { return Pixel { 1.0F, 0.0F, 0.0F, 1.0F }; });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 32U, .lower_hemisphere_color = { 0.0F, 0.0F, 1.0F } },
        35U);
    ASSERT_TRUE(products.has_value());
    const auto up = ReadFace(**products, *(*products)->processed_cube, 2U, 0U);
    const auto down
      = ReadFace(**products, *(*products)->processed_cube, 3U, 0U);
    for (const auto& pixel : up)
      EXPECT_EQ(pixel, (Pixel { 1, 0, 0, 1 }));
    for (const auto& pixel : down)
      EXPECT_EQ(pixel, (Pixel { 0, 0, 1, 1 }));
    const auto sh = ReadBuffer<std::array<glm::vec4, 8>>(
      **products, *(*products)->diffuse_sh);
    const auto horizon
      = environment::internal::EvaluatePackedStaticSkyLightDiffuseSh(
        sh, { 1, 0, 0 });
    EXPECT_NEAR(horizon.r, 0.5F, 0.001F);
    EXPECT_NEAR(horizon.b, 0.5F, 0.001F);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, SubmittedAndUnsubmittedReadersRetainProducts)
  {
    auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2.0F, 1.0F, 0.5F, 1.0F }; });
    auto built
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 36U);
    ASSERT_TRUE(built.has_value());
    auto products = std::move(*built);
    auto readback
      = GetReadbackManager()->CreateBufferReadback("IBL retained generation");
    auto recording = Backend().AcquireCommandRecorder(
      Backend().QueueKeyFor(graphics::QueueRole::kGraphics),
      "IBL retained read", graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(static_cast<bool>(recording));
    ASSERT_TRUE(products->Attach(*recording, Backend().GetResourceRegistry()));
    recording->FlushBarriers();
    ASSERT_TRUE(
      readback->EnqueueCopy(*recording, *products->metadata, { 0U, 16U })
        .has_value());
    products.reset();
    source.registration = {};
    source.texture.reset();
    Backend().GetResourceRegistry().PollManagedRetirements();
    ASSERT_TRUE(recording.Submit());
    auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    environment::IblProductMetadata metadata;
    std::memcpy(&metadata, mapped->Bytes().data(), sizeof(metadata));
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_EQ(metadata.product_revision, 36U);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, BrdfLookupIsSharedAndRequiredByGenerations)
  {
    const auto first = PrepareBrdf();
    const auto second = PrepareBrdf();
    EXPECT_EQ(first, second);
    EXPECT_EQ(first->srv, second->srv);
    EXPECT_EQ(first->texture->GetDescriptor().format, Format::kRG16UNorm);
    EXPECT_EQ(first->texture->GetDescriptor().width, 128U);
    EXPECT_EQ(first->texture->GetDescriptor().height, 32U);
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2.0F, 0.5F, 1.0F, 1.0F }; });
    const auto missing = Processor().Process(
      source.texture, source.registration, {}, { .face_size = 16U }, 40U);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(),
      environment::internal::IblProcessError::kBrdfUnavailable);
    const auto products
      = Processor().Process(source.texture, source.registration, first,
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 41U);
    ASSERT_TRUE(products.has_value());
    EXPECT_EQ((*products)->brdf, first);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, SurfaceEvaluationMatchesDiffuseAndSplitSumReference)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2.0F, 0.5F, 1.0F, 1.0F }; });
    BeginCapture();
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 42U);
    ASSERT_TRUE(products.has_value());
    auto inputs = std::vector<SurfaceInput> {};
    auto expected = std::vector<std::array<float, 6>> {};
    const auto brdf = environment::internal::GetIblBrdfLookup();
    for (const auto y : { 0U, 3U, 15U, 31U }) {
      for (const auto x : { 0U, 31U, 127U }) {
        for (auto material = 0U; material < 3U; ++material) {
          auto input = Surface(**products);
          const auto nv = (x + 0.5F) / 128.0F;
          input.view = { std::sqrt(1.0F - nv * nv), 0.0F, nv };
          input.roughness = (y + 0.5F) / 32.0F;
          input.light.tint_rgb = { 0.25F, 0.5F, 0.75F };
          input.light.radiance_scale = 1.5F;
          input.light.diffuse_intensity = 2.0F;
          input.light.specular_intensity = 3.0F;
          input.occlusion = 0.35F;
          input.metallic = material == 0U ? 0.0F : 1.0F;
          if (material == 1U)
            input.f0 = input.base_color;
          if (material == 2U) {
            input.f0 = { 0.002F, 0.001F, 0.0005F };
            input.base_color = input.f0;
          }
          const auto a = brdf[y * 128U + x][0] / 65535.0F;
          const auto b = brdf[y * 128U + x][1] / 65535.0F;
          constexpr auto radiance = std::array { 2.0F, 0.5F, 1.0F };
          auto reference = std::array<float, 6> {};
          for (auto channel = 0U; channel < 3U; ++channel) {
            const auto gain
              = radiance[channel] * input.light.tint_rgb[channel] * 1.5F;
            reference[channel] = gain * input.base_color[channel]
              * (1.0F - input.metallic) * input.occlusion * 2.0F;
            reference[channel + 3U] = gain * 3.0F
              * (input.f0[channel] * a
                + std::min(1.0F, 50.0F * input.f0[1]) * b);
          }
          inputs.push_back(input);
          expected.push_back(reference);
        }
      }
    }
    const auto actual = Shade(**products, inputs);
    EndCapture();
    for (auto index = 0U; index < inputs.size(); ++index) {
      SCOPED_TRACE(index);
      for (auto channel = 0U; channel < 6U; ++channel)
        EXPECT_NEAR(actual[index][channel], expected[index][channel],
          0.001F * std::max(0.01F, expected[index][channel]));
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, SurfaceGatesAreIndependentAndRejectWrongRevision)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1.0F, 1.0F, 1.0F, 1.0F }; });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 43U);
    ASSERT_TRUE(products.has_value());
    auto inputs = std::array<SurfaceInput, 6> {};
    inputs.fill(Surface(**products));
    inputs[1].light.diffuse_intensity = 0.0F;
    inputs[2].light.specular_intensity = 0.0F;
    inputs[3].occlusion = 0.0F;
    inputs[4].light.ibl_generation += 1U;
    inputs[5].light.enabled = 0U;
    const auto actual = Shade(**products, inputs);
    for (auto channel = 0U; channel < 3U; ++channel) {
      EXPECT_GT(actual[0][channel], 0.0F);
      EXPECT_GT(actual[0][channel + 3U], 0.0F);
      EXPECT_EQ(actual[1][channel], 0.0F);
      EXPECT_EQ(actual[1][channel + 3U], actual[0][channel + 3U]);
      EXPECT_EQ(actual[2][channel], actual[0][channel]);
      EXPECT_EQ(actual[2][channel + 3U], 0.0F);
      EXPECT_EQ(actual[3][channel], 0.0F);
      EXPECT_EQ(actual[3][channel + 3U], actual[0][channel + 3U]);
    }
    EXPECT_EQ(actual[4], (std::array<float, 6> {}));
    EXPECT_EQ(actual[5], (std::array<float, 6> {}));
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, SurfaceEvaluationRestoresHdrScaleExactlyOnce)
  {
    const auto source = MakeSource(16U,
      [](auto, auto, auto) { return Pixel { 1.0e8F, 2.0e6F, 8.0F, 1.0F }; });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 44U);
    ASSERT_TRUE(products.has_value());
    auto input = Surface(**products);
    input.base_color = { 1.0F, 1.0F, 1.0F };
    const auto nv = 127.5F / 128.0F;
    input.view = { std::sqrt(1.0F - nv * nv), 0.0F, nv };
    input.roughness = 15.5F / 32.0F;
    input.light.radiance_scale = 2.0F;
    const auto actual = Shade(**products, std::array { input }).front();
    const auto lookup = environment::internal::GetIblBrdfLookup();
    const auto a = lookup[15U * 128U + 127U][0] / 65535.0F;
    const auto b = lookup[15U * 128U + 127U][1] / 65535.0F;
    constexpr auto radiance = std::array { 1.0e8F, 2.0e6F, 8.0F };
    for (auto channel = 0U; channel < 3U; ++channel) {
      EXPECT_NEAR(
        actual[channel], radiance[channel] * 2.0F, radiance[channel] * 0.002F);
      const auto reference = radiance[channel] * 2.0F * (0.04F * a + b);
      EXPECT_NEAR(actual[channel + 3U], reference, reference * 0.002F);
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, OffscreenDielectricAndMetalRoughnessImage)
  {
    const auto source = MakeSource(128U, [](auto face, auto x, auto y) {
      const auto n = Direction(face, x, y, 128U);
      const auto sun_cosine
        = std::max(0.0F, (0.4F * n.x - 0.2F * n.y + n.z) / std::sqrt(1.2F));
      const auto sun = std::pow(sun_cosine, 128.0F);
      const auto sky = 0.3F + 0.7F * std::max(0.0F, n.z);
      return Pixel { 0.1F * sky + 40.0F * sun, 0.2F * sky + 24.0F * sun,
        0.4F * sky + 12.0F * sun, 1.0F };
    });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 128U, .lower_hemisphere_solid_color = false }, 45U);
    ASSERT_TRUE(products.has_value());
    constexpr auto width = 256U;
    constexpr auto height = 64U;
    auto inputs = std::vector<SurfaceInput>(width * height);
    for (auto y = 0U; y < height; ++y) {
      for (auto x = 0U; x < width; ++x) {
        auto input = Surface(**products);
        const auto nx = (static_cast<float>(x % 64U) + 0.5F - 32.0F) / 28.0F;
        const auto ny = (32.0F - static_cast<float>(y) - 0.5F) / 28.0F;
        const auto radius_squared = nx * nx + ny * ny;
        input.normal = radius_squared < 1.0F
          ? std::array { nx, ny, std::sqrt(1.0F - radius_squared) }
          : std::array { 0.0F, 0.0F, 0.0F };
        const auto material = x / 64U;
        constexpr auto roughness = std::array { 0.3F, 0.02F, 0.35F, 0.9F };
        input.roughness = roughness[material];
        input.metallic = material == 0U ? 0.0F : 1.0F;
        input.base_color = material == 0U ? std::array { 0.55F, 0.2F, 0.07F }
                                          : std::array { 0.95F, 0.64F, 0.54F };
        if (material != 0U)
          input.f0 = input.base_color;
        inputs[y * width + x] = input;
      }
    }
    const auto pixels = Shade(**products, inputs);
    auto peak = std::array<float, 4> {};
    auto image = std::vector<float>(width * height * 3U);
    for (auto y = 0U; y < height; ++y) {
      for (auto x = 0U; x < width; ++x) {
        for (auto channel = 0U; channel < 3U; ++channel) {
          const auto& pixel = pixels[y * width + x];
          const auto value = pixel[channel] + pixel[channel + 3U];
          ASSERT_TRUE(std::isfinite(value));
          ASSERT_GE(value, 0.0F);
          peak[x / 64U] = std::max(peak[x / 64U], value);
          image[((height - 1U - y) * width + x) * 3U + channel] = value;
        }
      }
    }
    EXPECT_GT(peak[0], 0.02F);
    EXPECT_GT(peak[1], 2.0F * peak[2]);
    EXPECT_GT(peak[2], peak[3]);
    char* path = nullptr;
    std::size_t path_size = 0U;
    ASSERT_EQ(_dupenv_s(&path, &path_size, "OXYGEN_IBL_IMAGE"), 0);
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(path, &std::free);
    if (owned) {
      const auto destination = std::filesystem::path(owned.get());
      if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path());
      auto stream = std::ofstream(destination, std::ios::binary);
      ASSERT_TRUE(stream.good());
      stream << "PF\n" << width << " " << height << "\n-1.0\n";
      stream.write(reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size() * sizeof(float)));
      ASSERT_TRUE(stream.good());
    }
  }

  NOLINT_TEST_F(IblConvolutionGpuTest,
    SurfaceRecordingRetainsBrdfAfterCacheAndProductRelease)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1.0F, 1.0F, 1.0F, 1.0F }; });
    auto built
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 46U);
    ASSERT_TRUE(built.has_value());
    auto products = std::move(*built);
    const auto inputs = std::array { Surface(*products) };
    const auto result = Decode({ .records = std::as_bytes(std::span(inputs)),
      .stride = sizeof(SurfaceInput),
      .record_kind = 32U,
      .decoded_words = 6U,
      .count = 1U,
      .prepare = [&](graphics::CommandRecorder& recorder) {
        ASSERT_TRUE(
          products->Attach(recorder, Backend().GetResourceRegistry()));
        products.reset();
        brdf_resources_.reset();
        Backend().GetResourceRegistry().PollManagedRetirements();
      } });
    for (const auto word : result) {
      EXPECT_TRUE(std::isfinite(std::bit_cast<float>(word)));
      EXPECT_GT(std::bit_cast<float>(word), 0.0F);
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, NexusReuseKeepsPhysicalProductsAndRejectsLiveOwner)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 1U);
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1.0F, 0.5F, 0.25F, 1.0F }; });
    graphics::Texture* physical = nullptr;
    auto descriptor = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
    std::uint32_t generation = 0U;
    for (auto revision = 100U; revision < 112U; ++revision) {
      auto result = Processor().Process(source.texture, source.registration,
        PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, revision);
      ASSERT_TRUE(result.has_value());
      auto products = std::move(*result);
      const auto metadata = ReadBuffer<environment::IblProductMetadata>(
        *products, *products->metadata);
      EXPECT_EQ(metadata.product_revision, revision);
      EXPECT_EQ(metadata.processing_flags, 3U);
      if (physical) {
        EXPECT_EQ(products->specular_cube.get(), physical);
        EXPECT_EQ(products->specular_srv, descriptor);
      }
      physical = products->specular_cube.get();
      descriptor = products->specular_srv;
      EXPECT_EQ(products->slot.index, 0U);
      EXPECT_GT(products->slot.generation.get(), generation);
      generation = products->slot.generation.get();
      auto busy = Processor().Process(source.texture, source.registration,
        PrepareBrdf(), { .face_size = 16U }, 999U);
      ASSERT_FALSE(busy.has_value());
      EXPECT_EQ(
        busy.error(), environment::internal::IblProcessError::kPoolBusy);
      products.reset();
      WaitForQueueIdle();
      Backend().PollCompletedUses();
    }
    const auto stats = Processor().GetStats();
    EXPECT_EQ(stats.storage_creations, 1U);
    EXPECT_EQ(stats.allocated, 1U);
    EXPECT_EQ(stats.available, 1U);
    EXPECT_EQ(stats.reuse.reclaimed_count, 12U);
    EXPECT_EQ(stats.reuse.pending_count, 0U);
    EXPECT_EQ(stats.reuse.abandoned_retirements, 0U);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, UnsubmittedReaderPreventsReuseUntilDiscard)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 1U);
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1.0F, 1.0F, 1.0F, 1.0F }; });
    auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 120U);
    ASSERT_TRUE(result.has_value());
    auto products = std::move(*result);
    auto recording = Backend().AcquireCommandRecorder(
      Backend().QueueKeyFor(graphics::QueueRole::kGraphics),
      "IBL pending reader", graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(products->Attach(*recording, Backend().GetResourceRegistry()));
    products.reset();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto busy = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 121U);
    ASSERT_FALSE(busy.has_value());
    EXPECT_EQ(busy.error(), environment::internal::IblProcessError::kPoolBusy);
    EXPECT_EQ(Processor().GetStats().reuse.pending_count, 1U);
    recording.Discard();
    const auto next = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 122U);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ((*next)->slot.generation.get(), 2U);
    EXPECT_EQ(Processor().GetStats().storage_creations, 1U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, RetainedProductsSurvivePoolClosure)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1.0F, 1.0F, 1.0F, 1.0F }; });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 130U);
    ASSERT_TRUE(products.has_value());
    Processor().Close();
    const auto rejected = Processor().Process(source.texture,
      source.registration, PrepareBrdf(), { .face_size = 16U }, 131U);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(
      rejected.error(), environment::internal::IblProcessError::kClosed);
    processor_.reset();
    brdf_resources_.reset();
    const auto values = Shade(**products, std::array { Surface(**products) });
    for (const auto value : values.front())
      EXPECT_GT(value, 0.0F);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, ResizingAFreeSlotReplacesItsResources)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 1U);
    const auto source = MakeSource(
      32U, [](auto, auto, auto) { return Pixel { 1.0F, 1.0F, 1.0F, 1.0F }; });
    auto first = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 140U);
    ASSERT_TRUE(first.has_value());
    auto old_texture
      = std::weak_ptr<graphics::Texture>((*first)->specular_cube);
    first->reset();
    WaitForQueueIdle();
    const auto second = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 32U }, 141U);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ((*second)->specular_cube->GetDescriptor().width, 32U);
    EXPECT_TRUE(old_texture.expired());
    EXPECT_EQ(Processor().GetStats().storage_creations, 2U);
    EXPECT_EQ(Processor().GetStats().allocated, 1U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest,
    GgxMatchesIndependentCpuIntegrationAndHighSampleReference)
  {
    const auto source = MakeSource(128U, [](auto face, auto x, auto y) {
      const auto n = Direction(face, x, y, 128U);
      const auto peak = std::pow(std::max(0.0F, n.z), 32.0F);
      return Pixel { 1.0F + 0.5F * n.x, 1.0F + 0.5F * n.y, 0.2F + 5.0F * peak,
        1.0F };
    });
    const auto products
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 128U, .lower_hemisphere_solid_color = false }, 150U);
    ASSERT_TRUE(products.has_value());
    double peak_high_sample_error = 0.0;
    double squared_high_sample_error = 0.0;
    double peak_matching_error = 0.0;
    unsigned comparisons = 0U;
    for (const auto mip : { 0U, 2U, 4U, 5U, 7U }) {
      const auto size = 128U >> mip;
      const auto roughness = std::exp2((double(mip) + 2.0 - 7.0) / 1.2);
      for (auto face = 0U; face < 6U; ++face) {
        const auto values
          = ReadFace(**products, *(*products)->specular_cube, face, mip);
        const auto points = std::array { std::array { 0U, 0U },
          std::array { 0U, size / 2U }, std::array { size / 2U, size / 2U },
          std::array { size - 1U, size - 1U } };
        for (auto point = 0U; point < (size == 1U ? 1U : points.size());
          ++point) {
          const auto [x, y] = points[point];
          const auto n = Direction(face, x, y, size);
          const auto cube_normal
            = glm::normalize(glm::dvec3 { n.x, n.z, -n.y });
          const auto matching = Reference(**products,
            PrefilterReferencePlan(cube_normal, roughness, 128U, 7U,
              roughness < 0.1 ? 32U : 64U, (*products)->processed_srv));
          const auto high_sample = Reference(**products,
            PrefilterReferencePlan(cube_normal, roughness, 128U, 7U, 1024U,
              (*products)->processed_srv));
          for (auto c = 0U; c < 3U; ++c) {
            SCOPED_TRACE(::testing::Message()
              << "mip " << mip << " face " << face << " x " << x << " channel "
              << c);
            const auto value = values[y * size + x][c];
            // Native arithmetic plus FP16 storage, independent of the
            // separately measured 32/64 versus 1024-sample estimator error.
            EXPECT_NEAR(value, matching[c], 0.001 * std::max(1.0, matching[c]));
            peak_matching_error
              = std::max(peak_matching_error, std::abs(value - matching[c]));
            const auto error = std::abs(value - high_sample[c]) / 5.2;
            peak_high_sample_error = std::max(peak_high_sample_error, error);
            squared_high_sample_error += error * error;
            ++comparisons;
          }
        }
      }
    }
    // Quality envelope for this smooth-plus-directional 5.2-peak fixture.
    // Production retains the prescribed sample counts and footprint policy.
    EXPECT_LE(peak_high_sample_error, 0.05);
    EXPECT_LE(std::sqrt(squared_high_sample_error / comparisons), 0.01);
    RecordProperty(
      "matching_max_absolute_error", std::to_string(peak_matching_error));
    RecordProperty("reference_1024_peak_normalized_error",
      std::to_string(peak_high_sample_error));
    RecordProperty("reference_1024_normalized_rms",
      std::to_string(std::sqrt(squared_high_sample_error / comparisons)));
    RecordProperty("reference_scalar_comparisons", std::to_string(comparisons));
  }
} // namespace
} // namespace oxygen::vortex::testing
