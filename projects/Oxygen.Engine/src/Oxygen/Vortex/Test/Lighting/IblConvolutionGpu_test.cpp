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
#include <new>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <wrl/client.h>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
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
  auto DirectionAt(std::uint32_t face, float x, float y, std::uint32_t size)
    -> glm::vec3
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

  auto Direction(std::uint32_t face, std::uint32_t x, std::uint32_t y,
    std::uint32_t size) -> glm::vec3
  {
    return DirectionAt(face, float(x), float(y), size);
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

    auto BrdfResponse(const SurfaceInput& input) -> std::array<float, 2>
    {
      const auto lookup_input = std::array<float, 4> { 1.0F, input.roughness,
        std::bit_cast<float>(input.brdf_srv), 0.0F };
      const auto lut
        = Decode({ .records = std::as_bytes(std::span(lookup_input)),
          .stride = 16U,
          .record_kind = 31U,
          .decoded_words = 2U,
          .count = 1U });
      return { std::bit_cast<float>(lut[0]), std::bit_cast<float>(lut[1]) };
    }

    auto SelectedSpecular(const IblGpuProducts& products,
      std::span<const SurfaceInput> inputs) -> std::vector<std::uint32_t>
    {
      return Decode({ .records = std::as_bytes(inputs),
        .stride = sizeof(SurfaceInput),
        .record_kind = 37U,
        .decoded_words = 1U,
        .count = static_cast<std::uint32_t>(inputs.size()),
        .prepare = [&](graphics::CommandRecorder& recorder) {
          CHECK_F(products.Attach(recorder, Backend().GetResourceRegistry()));
        } });
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

    template <typename Value, typename Owner>
    auto ReadBuffer(const Owner& products, const graphics::Buffer& buffer)
      -> Value
    {
      auto readback
        = GetReadbackManager()->CreateBufferReadback("IBL product check");
      SubmitCommands(
        "IBL buffer readback", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(static_cast<bool>(
            products.Attach(recorder, Backend().GetResourceRegistry())));
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

    template <typename Owner>
    auto ReadFace(const Owner& products, const graphics::Texture& texture,
      std::uint32_t face, std::uint32_t mip) -> std::vector<Pixel>
    {
      const auto size = texture.GetDescriptor().width >> mip;
      auto readback
        = GetReadbackManager()->CreateTextureReadback("IBL face check");
      SubmitCommands(
        "IBL face readback", [&](graphics::CommandRecorder& recorder) {
          CHECK_F(static_cast<bool>(
            products.Attach(recorder, Backend().GetResourceRegistry())));
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
      const bool half_storage
        = texture.GetDescriptor().format == Format::kRGBA16Float;
      for (auto y = 0U; y < size; ++y) {
        for (auto x = 0U; x < size; ++x) {
          if (!half_storage) {
            std::memcpy(pixels[y * size + x].data(),
              mapped->Data() + y * mapped->Layout().row_pitch.get()
                + x * sizeof(Pixel),
              sizeof(Pixel));
            continue;
          }
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

    auto ExpectMatchingProducts(
      const IblGpuProducts& expected, const IblGpuProducts& actual) -> void
    {
      const auto a
        = ReadBuffer<std::array<glm::vec4, 8>>(expected, *expected.diffuse_sh);
      const auto b
        = ReadBuffer<std::array<glm::vec4, 8>>(actual, *actual.diffuse_sh);
      EXPECT_EQ(std::memcmp(a.data(), b.data(), sizeof(a)), 0);
      const auto ma = ReadBuffer<environment::IblProductMetadata>(
        expected, *expected.metadata);
      const auto mb
        = ReadBuffer<environment::IblProductMetadata>(actual, *actual.metadata);
      EXPECT_EQ(ma.processing_flags, 3U);
      EXPECT_EQ(mb.processing_flags, 3U);
      EXPECT_EQ(ma.source_radiance_scale, mb.source_radiance_scale);
      EXPECT_EQ(ma.average_brightness, mb.average_brightness);
      EXPECT_EQ(ma.maximum_half_gain, mb.maximum_half_gain);
      EXPECT_EQ(ma.precision_flags, mb.precision_flags);
      const auto pairs = std::array { std::pair { expected.processed_cube,
                                        actual.processed_cube },
        std::pair { expected.specular_cube, actual.specular_cube },
        std::pair { expected.processed_half_cube, actual.processed_half_cube },
        std::pair { expected.specular_half_cube, actual.specular_half_cube } };
      for (const auto& [reference, candidate] : pairs) {
        for (unsigned mip = 0U; mip <= expected.maximum_mip; ++mip) {
          for (unsigned face = 0U; face < 6U; ++face) {
            const auto ra = ReadFace(expected, *reference, face, mip);
            const auto rb = ReadFace(actual, *candidate, face, mip);
            ASSERT_EQ(ra.size(), rb.size());
            EXPECT_EQ(
              std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(Pixel)), 0);
          }
        }
      }
    }
  };

  // Fail only the producer's existing resource factories. Native allocations,
  // registrations, submission and retirement still run through D3D12/Graphics.
  class IblAllocationFaultGraphics final : public graphics::d3d12::Graphics {
  public:
    using Graphics::Graphics;

    auto FailAfter(const int allowed) -> void { remaining_ = allowed; }

    auto CreateTexture(const graphics::TextureDesc& desc) const
      -> std::shared_ptr<graphics::Texture> override
    {
      BeforeAllocation(desc.debug_name);
      return Graphics::CreateTexture(desc);
    }

    auto CreateBuffer(const graphics::BufferDesc& desc) const
      -> std::shared_ptr<graphics::Buffer> override
    {
      BeforeAllocation(desc.debug_name);
      return Graphics::CreateBuffer(desc);
    }

  private:
    auto BeforeAllocation(const std::string_view name) const -> void
    {
      if (remaining_ < 0 || !name.starts_with("IBL."))
        return;
      if (remaining_-- == 0)
        throw std::bad_alloc {};
    }
    mutable int remaining_ { -1 };
  };

  class IblProducerAllocationGpuTest : public IblConvolutionGpuTest {
  protected:
    auto CreateBackend(const SerializedBackendConfig& config,
      const SerializedPathFinderConfig& paths)
      -> std::shared_ptr<graphics::d3d12::Graphics> override
    {
      return std::make_shared<IblAllocationFaultGraphics>(config, paths);
    }

    auto Faults() -> IblAllocationFaultGraphics&
    {
      return static_cast<IblAllocationFaultGraphics&>(Backend());
    }

    auto MakeAtmosphereSource() -> environment::internal::IblSkySource
    {
      auto source = environment::internal::IblSkySource {};
      source.environment.atmosphere.enabled = 1U;
      auto sky_view = CreateTexture({ .width = 4U,
        .height = 4U,
        .format = Format::kRGBA32Float,
        .texture_type = TextureType::kTexture2D,
        .debug_name = "Allocation test sky LUT",
        .is_shader_resource = true });
      auto distant = CreateRegisteredBuffer(
        { .size_bytes = 16U, .debug_name = "Allocation test distant LUT" });
      working_sky_ = sky_view;
      working_distant_ = distant;
      UploadWorkingLuts({ 2, 1, 0.5F, 1 });
      source.sky_view = std::move(sky_view);
      source.distant_sky = std::move(distant);
      return source;
    }

    auto UploadWorkingLuts(const Pixel& pixel) -> void
    {
      const auto& sky_view = working_sky_;
      const auto& distant = working_distant_;
      const auto subresources = std::array<upload::UploadSubresource, 1> {};
      const auto plan
        = upload::UploadPlanner::PlanTexture2D({ .dst = sky_view },
          subresources, upload::UploadPolicy { QueueKeyFor() });
      CHECK_F(plan.has_value());
      auto staging
        = CreateRegisteredBuffer({ .size_bytes = plan->total_bytes + 16U,
          .memory = graphics::BufferMemory::kUpload,
          .debug_name = "Allocation test LUT upload" });
      const auto row = std::array { pixel, pixel, pixel, pixel };
      const auto& region = plan->regions.front();
      for (unsigned y = 0U; y < 4U; ++y)
        staging->Update(row.data(), sizeof(row),
          region.buffer_offset + y * std::uint64_t(region.buffer_row_pitch));
      staging->Update(pixel.data(), sizeof(pixel), plan->total_bytes);
      SubmitCommands(
        "Allocation test LUT upload", [&](graphics::CommandRecorder& recorder) {
          EnsureTracked(recorder, staging, ResourceStates::kGenericRead);
          EnsureTracked(recorder, sky_view,
            Backend()
              .TryGetKnownResourceState(sky_view->GetNativeResource())
              .value_or(ResourceStates::kCommon));
          EnsureTracked(recorder, distant,
            Backend()
              .TryGetKnownResourceState(distant->GetNativeResource())
              .value_or(ResourceStates::kCommon));
          recorder.RequireResourceState(*sky_view, ResourceStates::kCopyDest);
          recorder.RequireResourceState(*distant, ResourceStates::kCopyDest);
          recorder.FlushBarriers();
          recorder.CopyBufferToTexture(*staging, region, *sky_view);
          recorder.CopyBuffer(*distant, 0U, *staging, plan->total_bytes, 16U);
          recorder.RequireResourceStateFinal(
            *sky_view, ResourceStates::kShaderResource);
          recorder.RequireResourceStateFinal(
            *distant, ResourceStates::kShaderResource);
        });
    }

    std::shared_ptr<graphics::Texture> working_sky_;
    std::shared_ptr<graphics::Buffer> working_distant_;
  };

  NOLINT_TEST_F(IblProducerAllocationGpuTest,
    FactoryFailuresPreservePublishedProductsAndPermitRetry)
  {
    const Pixel radiance { 1, 0.5F, 0.25F, 1 };
    const auto source
      = MakeSource(32U, [&](auto, auto, auto) { return radiance; });
    const auto brdf = PrepareBrdf();
    auto sky = environment::internal::IblSkySource {};
    sky.environment.fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
      | kGpuFogFlagVisibleInRealTimeSkyCaptures;
    sky.environment.fog.primary_density = 0.01F;
    sky.environment.fog.fog_inscattering_luminance_rgb = { 2, 1, 0.5F };
    const auto atmosphere = MakeAtmosphereSource();
    unsigned total_failures = 0U;
    enum class Scenario { kCold, kResize, kFog, kAtmosphere, kFirstUse };
    const std::array cases { std::pair { Scenario::kCold, 9U },
      std::pair { Scenario::kResize, 9U }, std::pair { Scenario::kFog, 10U },
      std::pair { Scenario::kAtmosphere, 12U },
      std::pair { Scenario::kFirstUse, 9U } };
    for (const auto [scenario, expected_failures] : cases) {
      bool completed = false;
      unsigned failures = 0U;
      for (int allowed = 0; allowed < 32; ++allowed) {
        SCOPED_TRACE(::testing::Message()
          << "scenario=" << static_cast<unsigned>(scenario)
          << " allowed=" << allowed);
        processor_ = std::make_unique<IblGpuProcessor>(Backend());
        std::shared_ptr<const IblGpuProducts> retained;
        if (scenario != Scenario::kFirstUse) {
          const auto result = Processor().Process(source.texture,
            source.registration, brdf,
            { .face_size = 16U, .lower_hemisphere_solid_color = false }, 800U);
          ASSERT_TRUE(result);
          retained = *result;
        }
        WaitForQueueIdle();
        const auto live_resources
          = Backend().GetResourceRegistry().GetRegisteredResourceCount();
        if (scenario == Scenario::kResize) {
          // Populate a free slot at another size before forcing replacement.
          auto warm = Processor().Process(source.texture, source.registration,
            brdf, { .face_size = 8U }, 801U);
          ASSERT_TRUE(warm);
          warm->reset();
          WaitForQueueIdle();
        }
        const auto process = [&]() {
          const auto settings = IblProcessSettings { .face_size = 32U,
            .lower_hemisphere_solid_color = false };
          return scenario == Scenario::kFog
            ? Processor().ProcessSky(sky, brdf, settings, 802U)
            : scenario == Scenario::kAtmosphere
            ? Processor().ProcessSky(atmosphere, brdf, settings, 802U)
            : Processor().Process(
                source.texture, source.registration, brdf, settings, 802U);
        };
        Faults().FailAfter(allowed);
        auto candidate = process();
        Faults().FailAfter(-1);
        if (!candidate) {
          ++failures;
          EXPECT_EQ(candidate.error(),
            environment::internal::IblProcessError::kAllocationFailed);
          const auto stats = Processor().GetStats();
          EXPECT_EQ(stats.normal_in_use, retained ? 1U : 0U);
          EXPECT_EQ(stats.available,
            IblGpuProcessor::kMaximumSlots - (retained ? 1U : 0U));
          EXPECT_EQ(stats.reuse.pending_count, 0U);
          EXPECT_EQ(stats.reuse.abandoned_retirements, 0U);
          if (retained) {
            const auto pixels
              = ReadFace(*retained, *retained->processed_cube, 0U, 0U);
            EXPECT_TRUE(std::ranges::all_of(
              pixels, [&](const auto& pixel) { return pixel == radiance; }));
          }
          candidate = process();
          ASSERT_TRUE(candidate);
        } else {
          completed = true;
        }
        const auto metadata = ReadBuffer<environment::IblProductMetadata>(
          **candidate, *(*candidate)->metadata);
        EXPECT_EQ(metadata.product_revision, 802U);
        EXPECT_EQ(metadata.processing_flags, 3U);
        candidate->reset();
        WaitForQueueIdle();
        EXPECT_EQ(Processor().GetStats().normal_in_use, retained ? 1U : 0U);
        Processor().Close();
        Backend().PollCompletedUses();
        EXPECT_EQ(Backend().GetResourceRegistry().GetRegisteredResourceCount(),
          live_resources);
        if (retained)
          EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                      *retained, *retained->metadata)
                      .product_revision,
            800U);
        if (completed)
          break;
      }
      EXPECT_TRUE(completed);
      EXPECT_EQ(failures, expected_failures);
      total_failures += failures;
    }
    RecordProperty("producer_factory_failures", total_failures);
  }

  NOLINT_TEST_F(IblProducerAllocationGpuTest,
    TiledConstantGrowthRollsBackAndReusesTheSameProductSlot)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 1U);
    const auto source = MakeSource(
      32U, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; });
    const auto brdf = PrepareBrdf();
    auto whole = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 32U }, 950U);
    ASSERT_TRUE(whole);
    const auto storage = std::weak_ptr((*whole)->processed_cube);
    whole->reset();
    WaitForQueueIdle();
    const auto registered
      = Backend().GetResourceRegistry().GetRegisteredResourceCount();
    Faults().FailAfter(0);
    const auto rejected
      = Processor().Process(source.texture, source.registration, brdf,
        { .face_size = 32U, .dispatch_tile_size = 8U }, 951U);
    Faults().FailAfter(-1);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error(),
      environment::internal::IblProcessError::kAllocationFailed);
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    EXPECT_EQ(Processor().GetStats().available, 1U);
    EXPECT_EQ(Processor().GetStats().reuse.pending_count, 0U);
    Backend().PollCompletedUses();
    EXPECT_EQ(Backend().GetResourceRegistry().GetRegisteredResourceCount(),
      registered - 1U);
    for (const unsigned tile_size : { 8U, 16U, 0U }) {
      if (tile_size != 8U)
        Faults().FailAfter(
          0); // Smaller/whole work must reuse the grown buffer.
      auto built = Processor().Process(source.texture, source.registration,
        brdf, { .face_size = 32U, .dispatch_tile_size = tile_size },
        952U + tile_size);
      Faults().FailAfter(-1);
      ASSERT_TRUE(built);
      EXPECT_EQ((*built)->processed_cube, storage.lock());
      EXPECT_EQ(Processor().GetStats().storage_creations, 1U);
      EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                  **built, *(*built)->metadata)
                  .processing_flags,
        3U);
      built->reset();
      WaitForQueueIdle();
      EXPECT_EQ(Backend().GetResourceRegistry().GetRegisteredResourceCount(),
        registered);
    }
    for (const unsigned invalid : { 4U, 12U }) {
      const auto bad = Processor().Process(source.texture, source.registration,
        brdf, { .face_size = 32U, .dispatch_tile_size = invalid }, 970U);
      ASSERT_FALSE(bad);
      EXPECT_EQ(
        bad.error(), environment::internal::IblProcessError::kInvalidSource);
      EXPECT_EQ(Processor().GetStats().available, 1U);
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, MultiFrameJobPublishesOnlyCompleteProducts)
  {
    const auto source = MakeSource(32U, [](auto face, auto x, auto y) {
      return Pixel { float(1U + face + x), 0.5F * (1U + y), 0.125F, 1 };
    });
    const auto brdf = PrepareBrdf();
    const auto settings = IblProcessSettings { .face_size = 32U,
      .lower_hemisphere_solid_color = false,
      .dispatch_tile_size = 8U };
    const auto reference = Processor().Process(
      source.texture, source.registration, brdf, settings, 980U);
    ASSERT_TRUE(reference);
    BeginCapture();
    std::shared_ptr<environment::internal::IblGpuJob> job;
    std::uint32_t per_frame {};
    std::shared_ptr<const IblGpuProducts> completed;
    for (unsigned frame_index = 0U; frame_index < 4U; ++frame_index) {
      const auto sequence = frame::SequenceNumber { frame_index + 1U };
      const auto slot
        = frame::Slot { frame_index % frame::kFramesInFlight.get() };
      Backend().BeginFrame(sequence, slot);
      const ScopeGuard end_frame(
        [&]() noexcept { Backend().EndFrame(sequence, slot); });
      if (frame_index == 0U) {
        const auto begun = Processor().Begin(
          source.texture, source.registration, brdf, settings, 981U);
        ASSERT_TRUE(begun);
        job = *begun;
        const auto pending = Processor().PendingDispatches(*job).size();
        ASSERT_GT(pending, 4U);
        per_frame = static_cast<std::uint32_t>((pending + 3U) / 4U);
      }
      const auto advanced = Processor().Advance(job, per_frame);
      ASSERT_TRUE(advanced);
      EXPECT_LE(advanced->recorded_dispatches, per_frame);
      if (frame_index < 3U) {
        EXPECT_FALSE(advanced->products);
        EXPECT_GT(advanced->remaining_dispatches, 0U);
      } else {
        completed = advanced->products;
        EXPECT_EQ(advanced->remaining_dispatches, 0U);
      }
      const auto retained = ReadBuffer<environment::IblProductMetadata>(
        **reference, *(*reference)->metadata);
      EXPECT_EQ(retained.product_revision, 980U);
      EXPECT_EQ(retained.processing_flags, 3U);
    }
    EndCapture();
    ASSERT_TRUE(completed);
    EXPECT_TRUE(Processor().PendingDispatches(*job).empty());
    EXPECT_EQ(completed->revision, 981U);
    ExpectMatchingProducts(**reference, *completed);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, JobFastPathsRespectClosureAndBackendFault)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; });
    const auto brdf = PrepareBrdf();
    const auto complete = Processor().Begin(
      source.texture, source.registration, brdf, { .face_size = 16U }, 988U);
    ASSERT_TRUE(complete);
    const auto finished = Processor().Advance(*complete, UINT32_MAX);
    ASSERT_TRUE(finished);
    ASSERT_TRUE(finished->products);
    const auto pending = Processor().Begin(
      source.texture, source.registration, brdf, { .face_size = 16U }, 989U);
    ASSERT_TRUE(pending);
    WaitForQueueIdle();
    const auto backend = Backend().GetBackendLifetime();
    backend->MarkSubmissionFault();
    const ScopeGuard restore(
      [backend]() noexcept { backend->ClearSubmissionFault(); });
    const auto expect_closed = [&]() {
      for (const auto& job : { *complete, *pending }) {
        for (const auto limit : { 0U, UINT32_MAX }) {
          const auto denied = Processor().Advance(job, limit);
          ASSERT_FALSE(denied);
          EXPECT_EQ(
            denied.error(), environment::internal::IblProcessError::kClosed);
        }
      }
    };
    expect_closed();
    backend->ClearSubmissionFault();
    const auto idle = Processor().Advance(*pending, 0U);
    ASSERT_TRUE(idle);
    EXPECT_EQ(idle->recorded_dispatches, 0U);
    EXPECT_GT(idle->remaining_dispatches, 0U);
    EXPECT_FALSE(idle->products);
    Processor().Close();
    expect_closed();
  }

  NOLINT_TEST_F(
    IblProducerAllocationGpuTest, JobFreezesAtmosphereBeforeWorkingLutsChange)
  {
    auto source = MakeAtmosphereSource();
    const auto brdf = PrepareBrdf();
    const auto settings = IblProcessSettings { .face_size = 16U,
      .lower_hemisphere_solid_color = false,
      .dispatch_tile_size = 8U };
    const auto reference = Processor().ProcessSky(source, brdf, settings, 982U);
    ASSERT_TRUE(reference);
    const auto job = Processor().BeginSky(source, brdf, settings, 983U);
    ASSERT_TRUE(job);
    UploadWorkingLuts({ 64, 16, 8, 1 });
    source.environment.atmosphere.enabled = 0U;
    source.origin = { 1000, 2000, 3000 };
    std::shared_ptr<const IblGpuProducts> completed;
    while (!completed) {
      const auto result = Processor().Advance(*job, 17U);
      ASSERT_TRUE(result);
      completed = result->products;
    }
    ExpectMatchingProducts(**reference, *completed);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, RejectedJobBatchDoesNotAdvanceItsCursor)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; });
    const auto job = Processor().Begin(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U, .dispatch_tile_size = 8U }, 984U);
    ASSERT_TRUE(job);
    const auto before = Processor().PendingDispatches(**job).size();
    graphics::internal::SubmissionFaultTestAccess::FailNext(
      *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
    const auto rejected = Processor().Advance(*job, 7U);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error(),
      environment::internal::IblProcessError::kSubmissionFailed);
    EXPECT_EQ(Processor().PendingDispatches(**job).size(), before);
    const auto retry = Processor().Advance(*job, UINT32_MAX);
    ASSERT_TRUE(retry);
    ASSERT_TRUE(retry->products);
    EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                *retry->products, *retry->products->metadata)
                .product_revision,
      984U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, CancelledJobWaitsForItsSubmittedBatch)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 1U);
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; });
    const auto brdf = PrepareBrdf();
    auto job = Processor().Begin(source.texture, source.registration, brdf,
      { .face_size = 16U, .dispatch_tile_size = 8U }, 985U);
    ASSERT_TRUE(job);
    WaitForQueueIdle();
    Microsoft::WRL::ComPtr<ID3D12Fence> gate;
    ASSERT_TRUE(SUCCEEDED(Backend().GetCurrentDevice()->CreateFence(
      0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf()))));
    const ScopeGuard release_gate(
      [gate]() noexcept { static_cast<void>(gate->Signal(1U)); });
    auto* queue = static_cast<graphics::d3d12::CommandQueue*>(GetQueue().get());
    ASSERT_TRUE(SUCCEEDED(queue->GetCommandQueue()->Wait(gate.Get(), 1U)));
    const auto partial = Processor().Advance(*job, 3U);
    ASSERT_TRUE(partial);
    ASSERT_FALSE(partial->products);
    job->reset();
    EXPECT_EQ(Processor().GetStats().normal_in_use, 1U);
    const auto busy = Processor().Begin(
      source.texture, source.registration, brdf, { .face_size = 16U }, 986U);
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error(), environment::internal::IblProcessError::kPoolBusy);
    ASSERT_TRUE(SUCCEEDED(gate->Signal(1U)));
    WaitForQueueIdle();
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    EXPECT_EQ(Processor().GetStats().available, 1U);
    const auto retry = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 16U }, 987U);
    ASSERT_TRUE(retry);
    EXPECT_EQ(
      ReadBuffer<environment::IblProductMetadata>(**retry, *(*retry)->metadata)
        .product_revision,
      987U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, SpatialTilesMatchWholeDispatchProducts)
  {
    const auto source = MakeSource(32U, [](auto face, auto x, auto y) {
      return Pixel { 100000.0F * (1U + face) + 100.0F * x, 0.25F * (1U + y),
        0x1p-24F * (1U + x + y), 1.0F };
    });
    auto sky = environment::internal::IblSkySource {};
    sky.origin = { 0, 0, 2 };
    sky.environment.fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
      | kGpuFogFlagVisibleInRealTimeSkyCaptures;
    sky.environment.fog.primary_density = 0.001F;
    sky.environment.fog.primary_height_falloff = 0.005F;
    sky.environment.fog.fog_inscattering_luminance_rgb = { 4, 2, 1 };
    const auto brdf = PrepareBrdf();
    unsigned revision = 900U;
    unsigned comparisons = 0U;
    for (const bool captured : { false, true }) {
      const auto build = [&](unsigned tile_size) {
        const auto settings = IblProcessSettings { .face_size = 32U,
          .lower_hemisphere_solid_color = false,
          .dispatch_tile_size = tile_size };
        return captured
          ? Processor().ProcessSky(sky, brdf, settings, ++revision)
          : Processor().Process(
              source.texture, source.registration, brdf, settings, ++revision);
      };
      const auto whole = build(0U);
      ASSERT_TRUE(whole);
      const auto expected_sh
        = ReadBuffer<std::array<glm::vec4, 8>>(**whole, *(*whole)->diffuse_sh);
      const auto expected_metadata
        = ReadBuffer<environment::IblProductMetadata>(
          **whole, *(*whole)->metadata);
      for (const unsigned tile_size : { 8U, 16U }) {
        SCOPED_TRACE(::testing::Message()
          << "capture=" << captured << " tile=" << tile_size);
        if (!captured && tile_size == 8U)
          BeginCapture();
        const auto tiled = build(tile_size);
        ASSERT_TRUE(tiled);
        if (!captured && tile_size == 8U)
          EndCapture();
        const auto actual_sh = ReadBuffer<std::array<glm::vec4, 8>>(
          **tiled, *(*tiled)->diffuse_sh);
        EXPECT_EQ(
          std::memcmp(actual_sh.data(), expected_sh.data(), sizeof(actual_sh)),
          0);
        const auto actual_metadata
          = ReadBuffer<environment::IblProductMetadata>(
            **tiled, *(*tiled)->metadata);
        EXPECT_EQ(actual_metadata.source_radiance_scale,
          expected_metadata.source_radiance_scale);
        EXPECT_EQ(actual_metadata.average_brightness,
          expected_metadata.average_brightness);
        EXPECT_EQ(
          actual_metadata.processing_flags, expected_metadata.processing_flags);
        EXPECT_EQ(actual_metadata.maximum_half_gain,
          expected_metadata.maximum_half_gain);
        EXPECT_EQ(
          actual_metadata.precision_flags, expected_metadata.precision_flags);
        const auto pairs = std::array { std::pair { (*whole)->processed_cube,
                                          (*tiled)->processed_cube },
          std::pair { (*whole)->specular_cube, (*tiled)->specular_cube },
          std::pair {
            (*whole)->processed_half_cube, (*tiled)->processed_half_cube },
          std::pair {
            (*whole)->specular_half_cube, (*tiled)->specular_half_cube } };
        for (const auto& [expected, actual] : pairs) {
          for (unsigned mip = 0U; mip <= (*whole)->maximum_mip; ++mip) {
            for (unsigned face = 0U; face < 6U; ++face) {
              const auto a = ReadFace(**whole, *expected, face, mip);
              const auto b = ReadFace(**tiled, *actual, face, mip);
              ASSERT_EQ(a.size(), b.size());
              EXPECT_EQ(
                std::memcmp(a.data(), b.data(), a.size() * sizeof(Pixel)), 0);
              comparisons += static_cast<unsigned>(a.size()) * 4U;
            }
          }
        }
      }
    }
    RecordProperty("tiled_product_scalar_comparisons", comparisons);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, HalfMirrorsNarrowCompletedCanonicalChains)
  {
    const auto source = MakeSource(16U, [](auto face, auto x, auto y) {
      return Pixel { 0.001337F * (1U + face + x), 0.212345F * (1U + y),
        0x1p-30F * (1U + x + y), 1.0F };
    });
    const auto built
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 93U);
    ASSERT_TRUE(built.has_value());
    const auto& products = **built;
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      products, *products.metadata);
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_NE(metadata.processed_half_srv, kInvalidBindlessIndex);
    EXPECT_NE(metadata.specular_half_srv, kInvalidBindlessIndex);
    const auto pairs = std::array { std::pair { products.processed_cube,
                                      products.processed_half_cube },
      std::pair { products.specular_cube, products.specular_half_cube } };
    for (const auto& [canonical, half] : pairs) {
      ASSERT_EQ(canonical->GetDescriptor().format, Format::kRGBA32Float);
      ASSERT_EQ(half->GetDescriptor().format, Format::kRGBA16Float);
      for (auto mip = 0U; mip <= products.maximum_mip; ++mip) {
        for (auto face = 0U; face < 6U; ++face) {
          const auto reference = ReadFace(products, *canonical, face, mip);
          const auto narrowed = ReadFace(products, *half, face, mip);
          ASSERT_EQ(reference.size(), narrowed.size());
          for (auto i = 0U; i < reference.size(); ++i) {
            for (auto c = 0U; c < 4U; ++c) {
              EXPECT_EQ(
                narrowed[i][c], data::HalfFloat { reference[i][c] }.ToFloat());
            }
            EXPECT_GT(reference[i][2], 0.0F);
            EXPECT_EQ(narrowed[i][2], 0.0F);
          }
        }
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, PrecisionAdmissionCoversSmallAndLargeSpecifiedCubes)
  {
    // Tiny mips have partial thread groups. The 512-face chain also crosses
    // the precision scan's 65,535-group dispatch-row boundary.
    for (const auto size : { 1U, 4U, 512U }) {
      SCOPED_TRACE(size);
      const auto source = MakeSource(size, [](auto, auto, auto) {
        return Pixel { 1.0003F, 0.5001F, 0.25003F, 1.0F };
      });
      const auto built = Processor().Process(source.texture,
        source.registration, PrepareBrdf(),
        { .face_size = size, .lower_hemisphere_solid_color = false },
        1000U + size);
      ASSERT_TRUE(built.has_value());
      const auto& products = **built;
      const auto metadata = ReadBuffer<environment::IblProductMetadata>(
        products, *products.metadata);
      EXPECT_EQ(metadata.processing_flags, 3U);
      EXPECT_EQ(metadata.product_revision, 1000U + size);
      EXPECT_EQ(
        metadata.precision_flags, environment::kIblHalfCertificateComplete);
      EXPECT_GT(metadata.maximum_half_gain, 0x1p32F);
      auto input = Surface(products);
      input.roughness = 1.0F;
      const auto inputs = std::array { input };
      EXPECT_EQ(
        SelectedSpecular(products, inputs)[0], metadata.specular_half_srv);
      for (auto face = 0U; face < 6U; ++face) {
        const auto final_mip = ReadFace(
          products, *products.specular_half_cube, face, products.maximum_mip);
        ASSERT_EQ(final_mip.size(), 1U);
        EXPECT_EQ(final_mip[0], (Pixel { 1.0F, 0.5F, 0.25F, 1.0F }));
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, NativeHalfFilteringMatchesSurfaceSamplingDomain)
  {
    // Sample both stored representations directly. This measures native
    // filtering; it does not override the production admission certificate.
    auto directions = std::vector<std::array<float, 3>> {};
    for (auto face = 0U; face < 6U; ++face)
      for (auto y = 0U; y < 16U; ++y)
        for (auto x = 0U; x < 16U; ++x) {
          const auto n = Direction(face, x, y, 16U);
          directions.push_back({ n.x, n.z, -n.y });
        }
    constexpr auto edge = std::array { -1.00001F, -1.0F, -0.99999F, -0.999F,
      -0.5F, 0.0F, 0.5F, 0.999F, 0.99999F, 1.0F, 1.00001F };
    for (auto axis = 0U; axis < 3U; ++axis)
      for (const auto sign : { -1.0F, 1.0F })
        for (const auto u : edge)
          for (const auto v : edge) {
            auto direction = std::array<float, 3> {};
            direction[axis] = sign;
            direction[(axis + 1U) % 3U] = u;
            direction[(axis + 2U) % 3U] = v;
            directions.push_back(direction);
          }
    constexpr auto phases = std::array { -0x1p-9F, 0.0F, 0x1p-9F,
      0.5F - 0x1p-9F, 0.5F, 0.5F + 0x1p-9F, 1.0F - 0x1p-9F, 1.0F + 0x1p-9F };
    for (auto face = 0U; face < 6U; ++face)
      for (const auto x : { 0U, 63U, 127U })
        for (const auto y : { 0U, 63U, 127U })
          for (const auto u : phases)
            for (const auto v : phases) {
              const auto n
                = DirectionAt(face, float(x) + u, float(y) + v, 128U);
              directions.push_back({ n.x, n.z, -n.y });
            }
    auto lods = std::vector<float> { 7.0F };
    for (auto mip = 0U; mip < 7U; ++mip)
      for (const auto fraction :
        { 0.0F, 0x1p-9F, 0x1p-8F, 0.5F, 1.0F - 0x1p-8F, 1.0F - 0x1p-9F })
        lods.push_back(float(mip) + fraction);

    auto maximum_rgb_error = 0.0;
    auto maximum_ev_error = 0.0;
    auto consumer_maximum_rgb_error = 0.0;
    auto consumer_maximum_ev_error = 0.0;
    auto consumer_comparisons = std::uint64_t {};
    auto comparisons = std::uint64_t {};
    for (const bool checker : { false, true }) {
      const auto source = MakeSource(128U, [&](auto face, auto x, auto y) {
        if (checker) {
          const float value = ((x + y + face) & 1U) != 0U ? 1000.3F : 0.1337F;
          return Pixel { value, value * 0.713F, value * 0.317F, 1.0F };
        }
        const auto n = Direction(face, x, y, 128U);
        if (n.z < 0.0F)
          return Pixel { 0.0317F, 0.0413F, 0.0171F, 1.0F };
        const auto sun = 1000.0F
          * std::pow(
            std::max(0.0F, glm::dot(n, glm::normalize(glm::vec3 { 1, 2, 3 }))),
            64.0F);
        return Pixel { 4.1F + 16.3F * n.z * n.z + sun,
          7.3F + 24.1F * n.z * n.z + 0.6F * sun,
          12.7F + 40.3F * n.z * n.z + 0.2F * sun, 1.0F };
      });
      const auto built = Processor().Process(source.texture,
        source.registration, PrepareBrdf(),
        { .face_size = 128U, .lower_hemisphere_solid_color = false },
        checker ? 2002U : 2001U);
      ASSERT_TRUE(built.has_value());
      const auto& products = **built;
      const auto metadata = ReadBuffer<environment::IblProductMetadata>(
        products, *products.metadata);
      ASSERT_GT(metadata.maximum_half_gain, 1.0F);
      const auto surface = std::array { Surface(products) };
      EXPECT_EQ(
        SelectedSpecular(products, surface)[0], metadata.specular_half_srv);
      const auto mapping_input = std::array<float, 4> { 1.0F, 0.0F,
        float(products.maximum_mip), 0.04F };
      const auto mapping
        = Decode({ .records = std::as_bytes(std::span(mapping_input)),
          .stride = 16U,
          .record_kind = 30U,
          .decoded_words = 3U,
          .count = 1U });
      const auto maximum_consumer_lod = std::bit_cast<float>(mapping[0]);
      for (const auto descriptors : { std::array { products.processed_srv.get(),
                                        metadata.processed_half_srv },
             std::array {
               products.specular_srv.get(), metadata.specular_half_srv } }) {
        auto samples = std::vector<ReferenceSample> {};
        samples.reserve(directions.size() * lods.size() * 2U);
        for (const auto& direction : directions)
          for (const auto lod : lods)
            for (const auto descriptor : descriptors)
              samples.push_back({ direction, lod, descriptor });
        auto words = std::vector<std::uint32_t> {};
        words.reserve(samples.size() * 3U);
        // The probe has one thread per group. Keep both representations in
        // the same batch while respecting D3D12's per-dimension group limit.
        constexpr auto kBatchRecords = 65534U;
        for (std::size_t first = 0U; first < samples.size();
          first += kBatchRecords) {
          const auto batch = std::span(samples).subspan(first,
            std::min(std::size_t(kBatchRecords), samples.size() - first));
          const auto decoded = Decode({ .records = std::as_bytes(batch),
            .stride = sizeof(ReferenceSample),
            .record_kind = 33U,
            .decoded_words = 3U,
            .count = static_cast<std::uint32_t>(batch.size()),
            .prepare = [&](graphics::CommandRecorder& recorder) {
              CHECK_F(
                products.Attach(recorder, Backend().GetResourceRegistry()));
            } });
          words.insert(words.end(), decoded.begin(), decoded.end());
        }
        auto chain_rgb_error = 0.0;
        auto chain_ev_error = 0.0;
        auto consumer_ev_error = 0.0;
        auto failed_ev_pairs = std::uint64_t {};
        auto worst_sample = ReferenceSample {};
        auto worst_channel = 0U;
        auto worst_values = std::array<double, 2> {};
        for (auto index = 0U; index < words.size(); index += 6U)
          for (auto channel = 0U; channel < 3U; ++channel) {
            const double reference
              = std::bit_cast<float>(words[index + channel]);
            const double narrowed
              = std::bit_cast<float>(words[index + channel + 3U]);
            ASSERT_GT(reference, 0.0);
            ASSERT_GT(narrowed, 0.0);
            ASSERT_TRUE(std::isfinite(reference) && std::isfinite(narrowed));
            maximum_rgb_error = std::max(
              maximum_rgb_error, std::abs(narrowed - reference) / reference);
            maximum_ev_error = std::max(
              maximum_ev_error, std::abs(std::log2(narrowed / reference)));
            chain_rgb_error = std::max(
              chain_rgb_error, std::abs(narrowed - reference) / reference);
            const auto ev_error = std::abs(std::log2(narrowed / reference));
            const auto sampled_lod = samples[index / 3U].lod;
            if (descriptors[0] == products.specular_srv.get()
              && sampled_lod <= maximum_consumer_lod) {
              consumer_ev_error = std::max(consumer_ev_error, ev_error);
              consumer_maximum_ev_error
                = std::max(consumer_maximum_ev_error, ev_error);
              consumer_maximum_rgb_error = std::max(consumer_maximum_rgb_error,
                std::abs(narrowed - reference) / reference);
              ++consumer_comparisons;
            }
            if (ev_error > 1.0 / 1024.0)
              ++failed_ev_pairs;
            if (ev_error > chain_ev_error) {
              chain_ev_error = ev_error;
              worst_sample = samples[index / 3U];
              worst_channel = channel;
              worst_values = { reference, narrowed };
            }
            ++comparisons;
          }
        const auto prefix = std::string(checker ? "checker_" : "sky_")
          + (descriptors[0] == products.processed_srv.get() ? "processed_"
                                                            : "specular_");
        RecordProperty(
          prefix + "max_relative_rgb", std::to_string(chain_rgb_error));
        RecordProperty(prefix + "max_ev", std::to_string(chain_ev_error));
        RecordProperty(
          prefix + "consumer_max_ev", std::to_string(consumer_ev_error));
        RecordProperty(
          prefix + "pairs_above_1_1024_stop", std::to_string(failed_ev_pairs));
        RecordProperty(prefix + "worst_lod", std::to_string(worst_sample.lod));
        RecordProperty(prefix + "worst_direction",
          std::to_string(worst_sample.direction[0]) + ","
            + std::to_string(worst_sample.direction[1]) + ","
            + std::to_string(worst_sample.direction[2]));
        RecordProperty(prefix + "worst_channel", std::to_string(worst_channel));
        RecordProperty(
          prefix + "worst_reference", std::to_string(worst_values[0]));
        RecordProperty(prefix + "worst_half", std::to_string(worst_values[1]));
        RecordProperty(prefix + "worst_direction_bits",
          std::to_string(
            std::bit_cast<std::uint32_t>(worst_sample.direction[0]))
            + ","
            + std::to_string(
              std::bit_cast<std::uint32_t>(worst_sample.direction[1]))
            + ","
            + std::to_string(
              std::bit_cast<std::uint32_t>(worst_sample.direction[2])));
        RecordProperty(prefix + "worst_lod_bits",
          std::to_string(std::bit_cast<std::uint32_t>(worst_sample.lod)));
        RecordProperty(prefix + "worst_reference_bits",
          std::to_string(std::bit_cast<std::uint32_t>(float(worst_values[0]))));
        RecordProperty(prefix + "worst_half_bits",
          std::to_string(std::bit_cast<std::uint32_t>(float(worst_values[1]))));
      }
    }
    // Broader samples remain diagnostic evidence. Production samples only the
    // prefiltered cube, over its roughness mapping's reachable LOD interval.
    EXPECT_GT(consumer_comparisons, 0U);
    EXPECT_LE(consumer_maximum_rgb_error, 0.0025);
    EXPECT_LE(consumer_maximum_ev_error, 2.0 / 1024.0);
    RecordProperty(
      "qualified_scalar_pairs", std::to_string(consumer_comparisons));
    RecordProperty("qualified_max_relative_rgb_error",
      std::to_string(consumer_maximum_rgb_error));
    RecordProperty(
      "qualified_max_ev_error", std::to_string(consumer_maximum_ev_error));
    RecordProperty(
      "native_half_filter_scalar_pairs", std::to_string(comparisons));
    RecordProperty("native_half_filter_max_relative_rgb_error",
      std::to_string(maximum_rgb_error));
    RecordProperty(
      "native_half_filter_max_ev_error", std::to_string(maximum_ev_error));
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, QualifiedConstantUsesHalfForActualSurfaceEvaluation)
  {
    const auto source = MakeSource(16U, [](auto, auto, auto) {
      return Pixel { 1.0003F, 0.5001F, 0.25003F, 1.0F };
    });
    const auto built
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 94U);
    ASSERT_TRUE(built.has_value());
    const auto& products = **built;
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      products, *products.metadata);
    EXPECT_EQ(
      metadata.precision_flags, environment::kIblHalfCertificateComplete);
    EXPECT_GT(metadata.maximum_half_gain, 0x1p32F);
    auto input = Surface(products);
    input.f0 = { 1.0F, 1.0F, 1.0F };
    input.metallic = 1.0F;
    const auto inputs = std::array { input };
    EXPECT_EQ(
      SelectedSpecular(products, inputs)[0], metadata.specular_half_srv);
    auto invalid = std::array<SurfaceInput, 5> {};
    invalid.fill(input);
    const auto infinity = std::numeric_limits<float>::infinity();
    for (auto& value : invalid)
      value.light.tint_rgb = { 0.0F, 0.0F, 0.0F };
    invalid[0].light.radiance_scale = infinity;
    invalid[1].f0 = { 0.0F, 0.0F, 0.0F };
    invalid[1].light.tint_rgb = { infinity, 0.0F, 0.0F };
    invalid[2].f0 = { infinity, 0.0F, 0.0F };
    invalid[3].light.specular_intensity = infinity;
    invalid[4].light.radiance_scale = -1.0F;
    for (const auto descriptor : SelectedSpecular(products, invalid))
      EXPECT_EQ(descriptor, products.specular_srv.get());
    const auto shaded = Shade(products, inputs)[0];
    const auto response = BrdfResponse(input);
    const auto brdf = response[0] + response[1];
    EXPECT_NEAR(shaded[3], 1.0F * brdf, 2.0e-6F);
    EXPECT_NEAR(shaded[4], 0.5F * brdf, 2.0e-6F);
    EXPECT_NEAR(shaded[5], 0.25F * brdf, 2.0e-6F);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, IntensityTintAndLobeEditsPromoteWithoutRecapture)
  {
    const auto source = MakeSource(16U, [](auto, auto, auto) {
      return Pixel { 0x1p-50F, 0x1p-50F, 0x1p-50F, 1.0F };
    });
    const auto built
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 95U);
    ASSERT_TRUE(built.has_value());
    const auto& products = **built;
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      products, *products.metadata);
    EXPECT_EQ(
      metadata.precision_flags, environment::kIblHalfCertificateComplete);
    EXPECT_GT(metadata.maximum_half_gain, 1.0F);
    EXPECT_LT(metadata.maximum_half_gain, 4.0F);
    auto low = Surface(products);
    low.f0 = { 1.0F, 1.0F, 1.0F };
    low.metallic = 1.0F;
    auto intensity = low;
    intensity.light.radiance_scale = 0x1p50F;
    auto tint = low;
    tint.light.tint_rgb = { 0x1p50F, 0.0F, 0.0F };
    auto lobe = low;
    lobe.light.specular_intensity = 0x1p50F;
    auto material = low;
    material.f0 = { 0x1p50F, 0x1p50F, 0x1p50F };
    const auto inputs
      = std::array { low, intensity, tint, lobe, low, material };
    const auto storage = Processor().GetStats().storage_creations;
    const auto selected = SelectedSpecular(products, inputs);
    EXPECT_EQ(selected,
      (std::vector<std::uint32_t> { metadata.specular_half_srv,
        products.specular_srv.get(), products.specular_srv.get(),
        products.specular_srv.get(), metadata.specular_half_srv,
        products.specular_srv.get() }));
    const auto shaded = Shade(products, inputs);
    EXPECT_EQ(shaded[0][3], 0.0F);
    const auto response = BrdfResponse(low);
    EXPECT_NEAR(shaded[1][3], response[0] + response[1], 2.0e-6F);
    EXPECT_NEAR(shaded[5][3], response[0], 2.0e-6F);
    EXPECT_NEAR(shaded[2][3], shaded[1][3], 1.0e-6F);
    EXPECT_EQ(shaded[2][4], 0.0F);
    EXPECT_NEAR(shaded[3][3], shaded[1][3], 1.0e-6F);
    EXPECT_EQ(shaded[4][3], 0.0F);
    EXPECT_EQ(Processor().GetStats().storage_creations, storage);
    EXPECT_EQ(products.revision, 95U);
  }

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

  NOLINT_TEST_F(IblConvolutionGpuTest,
    FullPrecisionProductsPreserveWideAndAmplifiedTinyRadiance)
  {
    unsigned revision = 300U;
    for (const bool wide : { false, true }) {
      const Pixel color = wide ? Pixel { 0x1p30F, 0x1p-24F, 0.5F, 1.0F }
                               : Pixel { 0x1p-50F, 0x1p-50F, 0x1p-50F, 1.0F };
      const auto source
        = MakeSource(16U, [&](auto, auto, auto) { return color; });
      const auto result = Processor().Process(source.texture,
        source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false },
        ++revision);
      ASSERT_TRUE(result.has_value());
      const auto& products = **result;
      const auto metadata = ReadBuffer<environment::IblProductMetadata>(
        products, *products.metadata);
      EXPECT_EQ(metadata.processing_flags, 3U);
      const double gain = wide ? 1.0 : 0x1p50;
      for (const auto& texture :
        { products.processed_cube, products.specular_cube }) {
        ASSERT_EQ(texture->GetDescriptor().format, Format::kRGBA32Float);
        for (unsigned face = 0U; face < 6U; ++face)
          for (unsigned mip = 0U; mip <= products.maximum_mip; ++mip) {
            const auto pixels = ReadFace(products, *texture, face, mip);
            for (const auto& pixel : pixels)
              for (unsigned c = 0U; c < 3U; ++c) {
                const double expected = color[c] * gain;
                EXPECT_NEAR(static_cast<double>(pixel[c])
                    * metadata.source_radiance_scale * gain,
                  expected, std::abs(expected) * 2.0e-5);
              }
          }
      }
    }
  }

  NOLINT_TEST_F(IblConvolutionGpuTest,
    CapturedFogProducesCompleteHdrProductsWithoutAtmosphere)
  {
    auto source = environment::internal::IblSkySource {};
    source.origin = { 0.0F, 0.0F, 1.0F };
    auto& fog = source.environment.fog;
    fog.primary_density = 0.01F;
    fog.primary_height_falloff = 0.0F;
    fog.min_transmittance = 0.25F;
    fog.max_opacity = 0.75F;
    fog.fog_inscattering_luminance_rgb = { 1.0e8F, 2.0e6F, 8.0F };
    // Display-only systems deliberately carry values that must never enter
    // captured lighting. Atmosphere-off capture owns no LUT resources.
    source.environment.sky_sphere.enabled = 1U;
    source.environment.sky_sphere.solid_color_rgb = { 100.0F, 200.0F, 300.0F };
    source.environment.sky_light.enabled = 1U;
    unsigned revision = 200U;
    for (const bool main_pass : { false, true }) {
      for (const bool capture_visible : { true, false }) {
        fog.flags = kGpuFogFlagEnabled | kGpuFogFlagHeightFogEnabled
          | (main_pass ? kGpuFogFlagRenderInMainPass : 0U)
          | (capture_visible ? kGpuFogFlagVisibleInRealTimeSkyCaptures : 0U);
        if (!main_pass && capture_visible)
          BeginCapture();
        const auto products = Processor().ProcessSky(source, PrepareBrdf(),
          { .face_size = 128U, .lower_hemisphere_solid_color = false },
          ++revision);
        ASSERT_TRUE(products.has_value());
        if (!main_pass && capture_visible)
          EndCapture();
        const auto metadata = ReadBuffer<environment::IblProductMetadata>(
          **products, *(*products)->metadata);
        EXPECT_EQ(metadata.processing_flags, 3U);
        EXPECT_EQ(metadata.product_revision, revision);
        const auto expected = std::array { capture_visible ? 7.5e7F : 0.0F,
          capture_visible ? 1.5e6F : 0.0F, capture_visible ? 6.0F : 0.0F };
        EXPECT_NEAR(metadata.source_radiance_scale,
          std::max(1.0F, expected[0] / 65504.0F), 0.001F);
        for (const auto& texture :
          { (*products)->processed_cube, (*products)->specular_cube }) {
          for (unsigned mip = 0U; mip <= (*products)->maximum_mip; ++mip) {
            for (unsigned face = 0U; face < 6U; ++face) {
              SCOPED_TRACE(::testing::Message()
                << texture->GetDescriptor().debug_name << " face=" << face
                << " mip=" << mip);
              const auto pixels = ReadFace(**products, *texture, face, mip);
              for (const auto& pixel : pixels)
                for (unsigned c = 0U; c < 3U; ++c) {
                  const float normalized
                    = expected[c] / metadata.source_radiance_scale;
                  // Source storage, native filtering and output storage round
                  // in binary16. Bound the combined error in that domain;
                  // a percentage of scene radiance ignores its ULP spacing.
                  const float ulp = normalized == 0.0F
                    ? 0.0F
                    : std::ldexp(
                        1.0F, std::max(-24, std::ilogb(normalized) - 10));
                  ASSERT_NEAR(pixel[c], normalized, 2.0F * ulp);
                }
            }
          }
        }
        const auto sh = ReadBuffer<std::array<glm::vec4, 8>>(
          **products, *(*products)->diffuse_sh);
        const auto diffuse
          = environment::internal::EvaluatePackedStaticSkyLightDiffuseSh(
              sh, { 0, 0, 1 })
          * metadata.source_radiance_scale;
        for (unsigned c = 0U; c < 3U; ++c)
          EXPECT_NEAR(
            diffuse[c], expected[c], std::max(0.001F, expected[c] * 0.001F));
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, CapturedSkyRequiresLutsAndRejectsSourceRotation)
  {
    auto source = environment::internal::IblSkySource {};
    const auto brdf = PrepareBrdf();
    source.environment.atmosphere.enabled = 1U;
    const auto missing
      = Processor().ProcessSky(source, brdf, { .face_size = 16U }, 210U);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(
      missing.error(), environment::internal::IblProcessError::kInvalidSource);
    source.environment.atmosphere.enabled = 0U;
    const auto rotated = Processor().ProcessSky(source, brdf,
      { .face_size = 16U, .source_rotation_radians = 0.5F }, 211U);
    ASSERT_FALSE(rotated.has_value());
    EXPECT_EQ(
      rotated.error(), environment::internal::IblProcessError::kInvalidSource);
    EXPECT_EQ(Processor().GetStats().storage_creations, 0U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest,
    AbsentCapturedSourcesStayZeroWithColoredLowerHemisphere)
  {
    auto source = environment::internal::IblSkySource {};
    source.environment.fog.primary_density = 0.01F;
    source.environment.fog.flags = kGpuFogFlagEnabled
      | kGpuFogFlagHeightFogEnabled | kGpuFogFlagRenderInMainPass;
    const auto products = Processor().ProcessSky(source, PrepareBrdf(),
      { .face_size = 16U, .lower_hemisphere_color = { 10.0F, 20.0F, 30.0F } },
      215U);
    ASSERT_TRUE(products.has_value());
    const auto metadata = ReadBuffer<environment::IblProductMetadata>(
      **products, *(*products)->metadata);
    EXPECT_EQ(metadata.processing_flags, 3U);
    EXPECT_FLOAT_EQ(metadata.average_brightness, 0.0F);
    for (unsigned face = 0U; face < 6U; ++face) {
      for (const auto& texture :
        { (*products)->processed_cube, (*products)->specular_cube }) {
        const auto pixels = ReadFace(**products, *texture, face, 0U);
        for (const auto& pixel : pixels)
          EXPECT_EQ(pixel, (Pixel { 0.0F, 0.0F, 0.0F, 1.0F }));
      }
    }
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, CapturedFogSnapshotsRemainIndependentUntilRetired)
  {
    processor_ = std::make_unique<IblGpuProcessor>(Backend(), 2U);
    auto source = environment::internal::IblSkySource {};
    source.origin = { 0.0F, 0.0F, 1.0F };
    source.environment.fog.flags = kGpuFogFlagEnabled
      | kGpuFogFlagHeightFogEnabled | kGpuFogFlagVisibleInRealTimeSkyCaptures;
    source.environment.fog.primary_density = 0.01F;
    source.environment.fog.fog_inscattering_luminance_rgb
      = { 1.0F, 2.0F, 3.0F };
    const auto settings = IblProcessSettings { .face_size = 16U,
      .lower_hemisphere_solid_color = false };
    auto first = Processor().ProcessSky(source, PrepareBrdf(), settings, 220U);
    ASSERT_TRUE(first.has_value());
    source.environment.fog.fog_inscattering_luminance_rgb
      = { 4.0F, 5.0F, 6.0F };
    auto second = Processor().ProcessSky(source, PrepareBrdf(), settings, 221U);
    ASSERT_TRUE(second.has_value());
    source.environment.fog.fog_inscattering_luminance_rgb = {};
    const auto busy
      = Processor().ProcessSky(source, PrepareBrdf(), settings, 222U);
    ASSERT_FALSE(busy.has_value());
    EXPECT_EQ(busy.error(), environment::internal::IblProcessError::kPoolBusy);
    EXPECT_NE((*first)->slot.index, (*second)->slot.index);
    const auto first_pixel
      = ReadFace(**first, *(*first)->processed_cube, 0U, 0U).front();
    const auto second_pixel
      = ReadFace(**second, *(*second)->processed_cube, 0U, 0U).front();
    for (unsigned c = 0U; c < 3U; ++c) {
      EXPECT_FLOAT_EQ(first_pixel[c], static_cast<float>(c + 1U));
      EXPECT_FLOAT_EQ(second_pixel[c], static_cast<float>(c + 4U));
    }
    first->reset();
    second->reset();
    WaitForQueueIdle();
    const auto next
      = Processor().ProcessSky(source, PrepareBrdf(), settings, 223U);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(Processor().GetStats().storage_creations, 2U);
    const auto zero
      = ReadFace(**next, *(*next)->processed_cube, 0U, 0U).front();
    EXPECT_EQ(zero, (Pixel { 0.0F, 0.0F, 0.0F, 1.0F }));
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
    ASSERT_TRUE(readback
        ->EnqueueCopy(*recording, *products->metadata,
          { 0U, sizeof(environment::IblProductMetadata) })
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

  NOLINT_TEST_F(
    IblConvolutionGpuTest, CaptureAdmissionReservesNormalUpdateCapacity)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    const auto brdf = PrepareBrdf();
    const auto make = [&](std::uint32_t revision) {
      auto result = Processor().Process(source.texture, source.registration,
        brdf, { .face_size = 16U }, revision);
      CHECK_F(result.has_value());
      return std::move(*result);
    };
    auto first = make(501U);
    auto second = make(502U);
    auto third = make(503U);
    auto a = first->AcquireCapture();
    auto duplicate = first->AcquireCapture();
    auto b = second->AcquireCapture();
    ASSERT_TRUE(a && duplicate && b);
    EXPECT_EQ(Processor().GetStats().captured_generations, 2U);
    const auto busy = third->AcquireCapture();
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error(), environment::IblCaptureError::kBusy);
    first.reset();
    second.reset();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    EXPECT_EQ(Processor().GetStats().normal_in_use, 1U);
    auto normal = std::vector<std::shared_ptr<const IblGpuProducts>> { third };
    for (unsigned i = 1U; i < IblGpuProcessor::kNormalSlots; ++i)
      normal.push_back(make(503U + i));
    EXPECT_EQ(
      Processor().GetStats().normal_in_use, IblGpuProcessor::kNormalSlots);
    EXPECT_EQ(Processor().GetStats().available, 0U);
    const auto full = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 16U }, 599U);
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error(), environment::internal::IblProcessError::kPoolBusy);
    EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(*a, *a->Metadata())
                .product_revision,
      501U);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    *a = {};
    EXPECT_EQ(Processor().GetStats().captured_generations, 2U);
    *duplicate = {};
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    EXPECT_EQ(Processor().GetStats().available, 1U);
    const auto still_full = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 16U }, 599U);
    ASSERT_FALSE(still_full);
    EXPECT_EQ(
      still_full.error(), environment::internal::IblProcessError::kPoolBusy);
    auto c = third->AcquireCapture();
    ASSERT_TRUE(c);
    normal.pop_back();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    normal.push_back(make(599U));
    EXPECT_EQ(
      Processor().GetStats().storage_creations, IblGpuProcessor::kMaximumSlots);
    EXPECT_EQ(Processor().GetStats().captured_generations, 2U);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest,
    CaptureRecordingsHoldAdmissionThroughDiscardAndCompletion)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 601U);
    ASSERT_TRUE(result);
    auto product = std::move(*result);
    auto capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    product.reset();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto pending = AcquireRecorder("IBL capture discard",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(capture->Attach(*pending, Backend().GetResourceRegistry()));
    *capture = {};
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    pending.Discard();
    EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
    EXPECT_EQ(Processor().GetStats().available, IblGpuProcessor::kMaximumSlots);

    result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 602U);
    ASSERT_TRUE(result);
    product = std::move(*result);
    capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    product.reset();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto readback
      = GetReadbackManager()->CreateBufferReadback("IBL retained capture");
    auto submitted = AcquireRecorder("IBL capture completion",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(capture->Attach(*submitted, Backend().GetResourceRegistry()));
    submitted->FlushBarriers();
    ASSERT_TRUE(readback->EnqueueCopy(*submitted, *capture->Metadata(),
      { 0U, sizeof(environment::IblProductMetadata) }));
    *capture = {};
    Microsoft::WRL::ComPtr<ID3D12Fence> gate;
    ASSERT_TRUE(SUCCEEDED(Backend().GetCurrentDevice()->CreateFence(
      0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf()))));
    const auto release_gate
      = ScopeGuard([gate]() noexcept { static_cast<void>(gate->Signal(1U)); });
    auto* queue = static_cast<graphics::d3d12::CommandQueue*>(GetQueue().get());
    ASSERT_TRUE(SUCCEEDED(queue->GetCommandQueue()->Wait(gate.Get(), 1U)));
    ASSERT_TRUE(submitted.Submit());
    Backend().PollCompletedUses();
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    // GPU pins must not own Graphics: its queues own the submitted batch.
    const auto owners_while_pending = GetGraphicsShared().use_count();
    ASSERT_TRUE(SUCCEEDED(gate->Signal(1U)));
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    EXPECT_EQ(GetGraphicsShared().use_count(), owners_while_pending);
    auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped);
    auto metadata = environment::IblProductMetadata {};
    std::memcpy(&metadata, mapped->Bytes().data(), sizeof(metadata));
    EXPECT_EQ(metadata.product_revision, 602U);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
    EXPECT_EQ(Processor().GetStats().available, IblGpuProcessor::kMaximumSlots);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, RejectedCaptureSubmissionRetainsRetryableLease)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 650U);
    ASSERT_TRUE(result);
    auto product = std::move(*result);
    auto capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    product.reset();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto rejected = AcquireRecorder("IBL rejected capture",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(capture->Attach(*rejected, Backend().GetResourceRegistry()));
    graphics::internal::SubmissionFaultTestAccess::FailNext(
      *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
    EXPECT_EQ(rejected.SubmitWithReceipt().outcome,
      graphics::SubmissionOutcome::kDiscarded);
    EXPECT_FALSE(Backend().GetBackendLifetime()->IsFaulted());
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                *capture, *capture->Metadata())
                .product_revision,
      650U);
    *capture = {};
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
    EXPECT_EQ(Processor().GetStats().available, IblGpuProcessor::kMaximumSlots);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, BackendFaultGateRejectsCaptureAdmissionAndAttachment)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    const auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 660U);
    ASSERT_TRUE(result);
    const auto capture = (*result)->AcquireCapture();
    ASSERT_TRUE(capture);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto recording = AcquireRecorder("IBL faulted capture",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    const auto backend = Backend().GetBackendLifetime();
    // Exercise the existing admission gate without issuing uncertain GPU work.
    backend->MarkSubmissionFault();
    const auto restore
      = ScopeGuard([backend]() noexcept { backend->ClearSubmissionFault(); });
    const auto denied = (*result)->AcquireCapture();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), environment::IblCaptureError::kClosed);
    const auto attachment
      = capture->Attach(*recording, Backend().GetResourceRegistry());
    ASSERT_FALSE(attachment);
    EXPECT_EQ(attachment.error(), environment::IblCaptureError::kClosed);
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    recording.Discard();
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, RejectedProducerReturnsNormalCapacity)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    const auto brdf = PrepareBrdf();
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    graphics::internal::SubmissionFaultTestAccess::FailNext(
      *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
    const auto failed = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 16U }, 670U);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(),
      environment::internal::IblProcessError::kSubmissionFailed);
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    EXPECT_EQ(Processor().GetStats().available, IblGpuProcessor::kMaximumSlots);
    EXPECT_FALSE(Backend().GetBackendLifetime()->IsFaulted());
    const auto retry = Processor().Process(
      source.texture, source.registration, brdf, { .face_size = 16U }, 671U);
    ASSERT_TRUE(retry);
    EXPECT_EQ(
      ReadBuffer<environment::IblProductMetadata>(**retry, *(*retry)->metadata)
        .product_revision,
      671U);
    EXPECT_EQ(Processor().GetStats().storage_creations, 1U);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, UncertainCaptureClosesPoolAfterBackendRecovery)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 680U);
    ASSERT_TRUE(result);
    auto product = std::move(*result);
    auto capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto recording = AcquireRecorder("IBL uncertain capture",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(capture->Attach(*recording, Backend().GetResourceRegistry()));
    recording->FlushBarriers();
    graphics::internal::SubmissionFaultTestAccess::FailNext(*GetQueue(),
      graphics::internal::SubmissionFailurePoint::kAfterIssueBeforeMarker);
    EXPECT_EQ(recording.SubmitWithReceipt().outcome,
      graphics::SubmissionOutcome::kExecutionUncertain);
    EXPECT_TRUE(Backend().GetBackendLifetime()->IsFaulted());
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    ASSERT_TRUE(Backend().RecoverSubmissionFault());
    const auto denied = product->AcquireCapture();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), environment::IblCaptureError::kClosed);
    auto retry = AcquireRecorder("IBL poisoned capture retry",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    const auto attachment
      = capture->Attach(*retry, Backend().GetResourceRegistry());
    ASSERT_FALSE(attachment);
    EXPECT_EQ(attachment.error(), environment::IblCaptureError::kClosed);
    retry.Discard();
    product.reset();
    *capture = {};
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
    // Recovery never revives a poisoned generation; a fresh owner rebuilds.
    processor_.reset();
    brdf_resources_.reset();
    const auto rebuilt = Processor().Process(source.texture,
      source.registration, PrepareBrdf(), { .face_size = 16U }, 681U);
    ASSERT_TRUE(rebuilt);
    EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                **rebuilt, *(*rebuilt)->metadata)
                .product_revision,
      681U);
  }

  NOLINT_TEST_F(
    IblConvolutionGpuTest, DeviceLossReleasesCaptureAndClosesAdmission)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    auto result = Processor().Process(source.texture, source.registration,
      PrepareBrdf(), { .face_size = 16U }, 690U);
    ASSERT_TRUE(result);
    auto product = std::move(*result);
    auto capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    auto recording = AcquireRecorder("IBL capture device removal",
      graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(capture->Attach(*recording, Backend().GetResourceRegistry()));
    recording->FlushBarriers();
    *capture = {};
    const auto submitted = recording.SubmitWithReceipt();
    ASSERT_TRUE(submitted.receipt);
    EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
    graphics::internal::SubmissionFaultTestAccess::LoseDevice(*GetQueue());
    EXPECT_EQ(GetQueue()->QueryCompletion(*submitted.receipt),
      graphics::CompletionStatus::kDeviceLost);
    EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
    const auto denied = product->AcquireCapture();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), environment::IblCaptureError::kClosed);
    product.reset();
    EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
    EXPECT_FALSE(Backend().RecoverSubmissionFault());
    EXPECT_EQ(Backend().GetBackendLifetime()->State(),
      graphics::BackendLifecycle::kRetiring);
  }

#if defined(_MSC_VER) && defined(_DEBUG)
  NOLINT_TEST_F(IblConvolutionGpuTest, CaptureAllocationFailuresReturnAdmission)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    const auto product = Processor().Process(source.texture,
      source.registration, PrepareBrdf(), { .face_size = 16U }, 695U);
    ASSERT_TRUE(product);
    WaitForQueueIdle();
    Backend().PollCompletedUses();
    unsigned failures = 0U;
    bool succeeded = false;
    for (int allowed = 0; allowed < 16; ++allowed) {
      auto capture
        = Result<environment::IblCaptureLease, environment::IblCaptureError>(
          Err(environment::IblCaptureError::kUnavailable));
      {
        graphics::testing::HeapAllocationFailure failure(allowed);
        capture = (*product)->AcquireCapture();
      }
      if (capture) {
        EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
        *capture = {};
        succeeded = true;
      } else {
        EXPECT_EQ(
          capture.error(), environment::IblCaptureError::kAllocationFailed);
        ++failures;
      }
      EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
      EXPECT_EQ(Processor().GetStats().normal_in_use, 1U);
      EXPECT_EQ(
        Processor().GetStats().available, IblGpuProcessor::kMaximumSlots - 1U);
      if (succeeded)
        break;
    }
    EXPECT_GE(failures, 3U);
    EXPECT_TRUE(succeeded);
    const auto retry = (*product)->AcquireCapture();
    ASSERT_TRUE(retry);
    EXPECT_EQ(
      ReadBuffer<environment::IblProductMetadata>(*retry, *retry->Metadata())
        .product_revision,
      695U);
    RecordProperty("capture_allocation_failures", failures);
  }

  NOLINT_TEST_F(IblConvolutionGpuTest, CaptureRetirementDoesNotAllocate)
  {
    using graphics::testing::HeapAllocationFailure;
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 1, 0.5F, 0.25F, 1 }; });
    for (const bool submit : { false, true }) {
      auto result = Processor().Process(source.texture, source.registration,
        PrepareBrdf(), { .face_size = 16U }, 696U + unsigned(submit));
      ASSERT_TRUE(result);
      auto product = std::move(*result);
      auto capture = product->AcquireCapture();
      ASSERT_TRUE(capture);
      WaitForQueueIdle();
      auto recording = AcquireRecorder("IBL allocation-free retirement",
        graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
      ASSERT_TRUE(capture->Attach(*recording, Backend().GetResourceRegistry()));
      recording->FlushBarriers();
      unsigned resolved = 0U;
      auto* recorder = recording.operator->();
      recording->OnSubmission([recorder, &resolved](auto) {
        ++resolved;
        recorder->OnSubmission([&resolved](auto) { ++resolved; });
      });
      const auto rejected_before = HeapAllocationFailure::RejectedCount();
      {
        HeapAllocationFailure denied;
        product.reset();
        *capture = {};
      }
      EXPECT_EQ(Processor().GetStats().captured_generations, 1U);
      if (submit) {
        ASSERT_TRUE(recording.Submit());
        // Wait for hardware without retiring the submitted internal pins.
        const auto queue = GetQueue();
        queue->Wait(queue->SignalSubmittedWork());
        ASSERT_EQ(Processor().GetStats().captured_generations, 1U);
        HeapAllocationFailure denied;
        Backend().PollCompletedUses();
      } else {
        HeapAllocationFailure denied;
        recording.Discard();
      }
      EXPECT_EQ(HeapAllocationFailure::RejectedCount(), rejected_before);
      EXPECT_EQ(resolved, 2U);
      EXPECT_EQ(Processor().GetStats().captured_generations, 0U);
      EXPECT_EQ(Processor().GetStats().normal_in_use, 0U);
      EXPECT_EQ(
        Processor().GetStats().available, IblGpuProcessor::kMaximumSlots);
      EXPECT_EQ(Processor().GetStats().reuse.pending_count, 0U);
      EXPECT_EQ(Processor().GetStats().reuse.abandoned_retirements, 0U);
    }
  }
#endif

  NOLINT_TEST_F(
    IblConvolutionGpuTest, AdmittedCaptureSurvivesGracefulPoolClosure)
  {
    const auto source = MakeSource(
      16U, [](auto, auto, auto) { return Pixel { 2, 1, 0.5F, 1 }; });
    auto result
      = Processor().Process(source.texture, source.registration, PrepareBrdf(),
        { .face_size = 16U, .lower_hemisphere_solid_color = false }, 701U);
    ASSERT_TRUE(result);
    auto product = std::move(*result);
    auto capture = product->AcquireCapture();
    ASSERT_TRUE(capture);
    Processor().Close();
    const auto denied = product->AcquireCapture();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), environment::IblCaptureError::kClosed);
    product.reset();
    processor_.reset();
    brdf_resources_.reset();
    EXPECT_EQ(ReadBuffer<environment::IblProductMetadata>(
                *capture, *capture->Metadata())
                .product_revision,
      701U);
    EXPECT_EQ(ReadFace(*capture, *capture->SpecularCube(), 0U, 0U).front(),
      (Pixel { 2, 1, 0.5F, 1 }));
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
