//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_uint2.hpp>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Lighting/UnculledLightingFixture.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>

namespace oxygen::vortex::testing {
namespace {
  struct alignas(packing::kShaderDataFieldAlignment) ReferenceArguments {
    glm::mat4 inverse_view_projection { 1.0F };
    glm::vec3 camera_position { 0.0F };
    float receiver_plane_z { -1.0F };
    ShaderVisibleIndex lights_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex baseline_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex output_uav { kInvalidShaderVisibleIndex };
    std::uint32_t light_count { 0U };
    ShaderVisibleIndex normal_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex material_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex base_color_srv { kInvalidShaderVisibleIndex };
    std::uint32_t forward_shading { 0U };
    glm::uvec2 extent { 0U };
    float pre_exposure { 1.0F };
    std::uint32_t brdf_model_revision { 0U };
    ShaderVisibleIndex brdf_moments_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex brdf_mean_moments_srv { kInvalidShaderVisibleIndex };
    std::array<std::uint32_t, 2> reserved {};
  };
  // NOLINTBEGIN(*-magic-numbers)
  static_assert(sizeof(ReferenceArguments) == 144U);
  static_assert(offsetof(ReferenceArguments, inverse_view_projection) == 0U);
  static_assert(offsetof(ReferenceArguments, camera_position) == 64U);
  static_assert(offsetof(ReferenceArguments, receiver_plane_z) == 76U);
  static_assert(offsetof(ReferenceArguments, lights_srv) == 80U);
  static_assert(offsetof(ReferenceArguments, baseline_srv) == 84U);
  static_assert(offsetof(ReferenceArguments, output_uav) == 88U);
  static_assert(offsetof(ReferenceArguments, light_count) == 92U);
  static_assert(offsetof(ReferenceArguments, normal_srv) == 96U);
  static_assert(offsetof(ReferenceArguments, material_srv) == 100U);
  static_assert(offsetof(ReferenceArguments, base_color_srv) == 104U);
  static_assert(offsetof(ReferenceArguments, forward_shading) == 108U);
  static_assert(offsetof(ReferenceArguments, extent) == 112U);
  static_assert(offsetof(ReferenceArguments, pre_exposure) == 120U);
  static_assert(offsetof(ReferenceArguments, brdf_model_revision) == 124U);
  static_assert(offsetof(ReferenceArguments, brdf_moments_srv) == 128U);
  static_assert(offsetof(ReferenceArguments, brdf_mean_moments_srv) == 132U);
  static_assert(offsetof(ReferenceArguments, reserved) == 136U);
  // NOLINTEND(*-magic-numbers)

  auto ReferenceShader() -> graphics::ShaderRequest
  {
    return {
      .stage = ShaderType::kCompute,
      .source_path = "Tests/UnculledLightingReference.hlsl",
      .entry_point = "CS",
    };
  }
} // namespace

auto UnculledLightingGpuTest::SetUp() -> void
{
  exposure::ExposureLightingGpuTest::SetUp();
  auto file = std::ifstream(OXYGEN_UNCULLED_LIGHTING_PROBE, std::ios::binary);
  ASSERT_TRUE(file.good());
  file.seekg(0, std::ios::end);
  const auto bytes = static_cast<std::size_t>(file.tellg());
  ASSERT_GT(bytes, 0U);
  ASSERT_EQ(bytes % sizeof(std::uint32_t), 0U);
  auto code = std::vector<std::uint32_t>(bytes / sizeof(std::uint32_t));
  file.seekg(0);
  // The file API fills the object representation of owned shader words.
  file.read(
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
  ASSERT_TRUE(file.good());
  FailureBackend().SetShaderOverride(ReferenceShader(),
    std::make_shared<graphics::ShaderByteCode<std::vector<std::uint32_t>>>(
      std::move(code)));
}

auto UnculledLightingGpuTest::RecordUnculledReference(
  graphics::CommandRecorder& recorder, const ReferenceInput& input) -> void
{
  using graphics::BufferMemory;
  using graphics::BufferUsage;
  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  ASSERT_FALSE(input.lights.empty());
  reference_pixel_count_
    = static_cast<std::size_t>(input.extent.x) * input.extent.y;
  ASSERT_EQ(input.baseline.size(), reference_pixel_count_);
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  const auto publish
    = [&](const auto records, const char* name) -> ShaderVisibleIndex {
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = records.size_bytes(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = name,
    });
    buffer->Update(records.data(), records.size_bytes(), 0U);
    auto handle
      = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
        ResourceViewType::kStructuredBuffer_SRV);
    const auto slot = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(*buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, records.size_bytes() },
        .stride = sizeof(typename decltype(records)::element_type),
      });
    EnsureTracked(recorder, buffer, ResourceStates::kGenericRead);
    return slot;
  };
  auto args = ReferenceArguments {};
  const auto* scene_renderer
    = RendererPublicationProbe::GetSceneRenderer(*renderer_);
  const auto* lighting
    = RendererPublicationProbe::GetLightingService(*renderer_)
        ->InspectForwardLightBindings(scene_renderer->GetPublishedViewId());
  ASSERT_NE(lighting, nullptr);
  // Share only the immutable BRDF model. The reference's light list remains
  // authored independently of renderer selection and spatial publication.
  args.brdf_model_revision = lighting->brdf_model_revision;
  args.brdf_moments_srv = lighting->brdf_moments_srv;
  args.brdf_mean_moments_srv = lighting->brdf_mean_moments_srv;
  const auto& view_data
    = RendererPublicationProbe::GetViewConstants(*renderer_).GetSnapshot();
  args.inverse_view_projection = view_data.inverse_view_projection_matrix;
  args.camera_position = view_data.camera_position;
  args.extent = input.extent;
  args.forward_shading = input.forward_shading ? 1U : 0U;
  args.pre_exposure = input.pre_exposure;
  args.light_count = static_cast<std::uint32_t>(input.lights.size());
  args.lights_srv = publish(input.lights, "Unculled reference authored lights");
  args.baseline_srv
    = publish(input.baseline, "Unculled reference receiver coverage");
  if (!input.forward_shading) {
    auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto& bindings = owner->GetSceneTextureBindings();
    args.normal_srv = ShaderVisibleIndex { bindings.gbuffer_srvs.at(
      static_cast<std::size_t>(GBufferIndex::kNormal)) };
    args.material_srv = ShaderVisibleIndex { bindings.gbuffer_srvs.at(
      static_cast<std::size_t>(GBufferIndex::kMaterial)) };
    args.base_color_srv = ShaderVisibleIndex { bindings.gbuffer_srvs.at(
      static_cast<std::size_t>(GBufferIndex::kBaseColor)) };
    for (const auto index : {
           GBufferIndex::kNormal,
           GBufferIndex::kMaterial,
           GBufferIndex::kBaseColor,
         }) {
      recorder.RequireResourceState(owner->GetSceneTextures().GetGBuffer(index),
        ResourceStates::kShaderResource);
    }
  }
  const auto output_size = reference_pixel_count_ * sizeof(exposure::Pixel);
  auto output = CreateRegisteredBuffer({
    .size_bytes = output_size,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = "Unculled reference image",
  });
  auto output_handle = allocator.AllocateRaw(ResourceViewType::kRawBuffer_UAV,
    graphics::DescriptorVisibility::kShaderVisible);
  args.output_uav = allocator.GetShaderVisibleIndex(output_handle);
  Backend().GetResourceRegistry().RegisterView(*output,
    std::move(output_handle),
    graphics::BufferViewDescription {
      .view_type = ResourceViewType::kRawBuffer_UAV,
      .range = { 0U, output_size },
      .stride = 0U,
    });
  const auto arguments = PublishFixtureData(args);
  // A reference must not resolve a renderer lighting publication by accident.
  const auto invalid_view = ViewConstants::GpuData {};
  constexpr std::size_t cbv_alignment = 256U;
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { cbv_alignment }, BufferUsage::kConstant);
  view_buffer->Update(&invalid_view, sizeof(invalid_view), 0U);
  const auto pipeline
    = graphics::ComputePipelineDesc::Builder {}
        .SetComputeShader(ReferenceShader())
        .SetRootBindings(exposure::ExposureProbeRootBindings())
        .SetDebugName("Unculled lighting reference")
        .Build();
  EnsureTracked(recorder, output, ResourceStates::kCommon);
  recorder.RequireResourceState(*output, ResourceStates::kUnorderedAccess);
  recorder.FlushBarriers();
  recorder.SetPipelineState(pipeline);
  namespace root = bindless::generated::d3d12;
  recorder.SetComputeRootConstantBufferView(
    static_cast<std::uint32_t>(root::RootParam::kViewConstants),
    view_buffer->GetGPUVirtualAddress());
  const auto root_slot
    = static_cast<std::uint32_t>(root::RootParam::kRootConstants);
  recorder.SetComputeRoot32BitConstant(root_slot, 0U, 0U);
  recorder.SetComputeRoot32BitConstant(root_slot, arguments.get(), 1U);
  constexpr std::size_t group_width = 64U;
  recorder.Dispatch(
    static_cast<std::uint32_t>(
      (reference_pixel_count_ + group_width - 1U) / group_width),
    1U, 1U);
  reference_readback_
    = GetReadbackManager()->CreateBufferReadback("Unculled reference result");
  ASSERT_TRUE(
    reference_readback_->EnqueueCopy(recorder, *output, { 0U, output_size })
      .has_value());
}

auto UnculledLightingGpuTest::ReadUnculledReference(
  std::vector<exposure::Pixel>& result) -> void
{
  ASSERT_NE(reference_readback_, nullptr);
  const auto mapped = reference_readback_->MapNow();
  ASSERT_TRUE(mapped.has_value());
  result.resize(reference_pixel_count_);
  const auto bytes = std::as_writable_bytes(std::span(result));
  ASSERT_GE(mapped->Bytes().size(), bytes.size());
  std::memcpy(bytes.data(), mapped->Bytes().data(), bytes.size());
  reference_readback_.reset();
}

} // namespace oxygen::vortex::testing
