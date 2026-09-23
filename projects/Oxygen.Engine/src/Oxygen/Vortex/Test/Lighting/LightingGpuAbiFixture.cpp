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
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d3d12.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfMomentData.h>
#include <Oxygen/Vortex/Test/Lighting/LightingGpuAbiFixture.h>

namespace oxygen::vortex::testing {
namespace {

  namespace root = oxygen::bindless::generated::d3d12;
  using graphics::BufferMemory;
  using graphics::BufferUsage;
  using graphics::BufferViewDescription;
  using graphics::CommandRecorder;
  using graphics::ComputePipelineDesc;
  using graphics::DescriptorTableBinding;
  using graphics::DescriptorVisibility;
  using graphics::DirectBufferBinding;
  using graphics::IShaderByteCode;
  using graphics::PushConstantsBinding;
  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  using graphics::RootBindingDesc;
  using graphics::RootBindingItem;
  using graphics::ShaderByteCode;
  using graphics::ShaderRequest;
  using graphics::ShaderStageFlags;

  auto LoadProbe() -> std::shared_ptr<IShaderByteCode>
  {
    auto stream = std::ifstream(OXYGEN_LIGHTING_ABI_PROBE, std::ios::binary);
    CHECK_F(stream.good(), "Cannot load lighting ABI probe");
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    CHECK_GT_F(size, std::streampos { 0 });
    const auto bytes = static_cast<std::size_t>(size);
    CHECK_EQ_F(bytes % sizeof(std::uint32_t), 0U);
    auto code = std::vector<std::uint32_t>(bytes / sizeof(std::uint32_t));
    stream.seekg(0);
    // The stream API accepts char storage for the owned shader object bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.read(reinterpret_cast<char*>(code.data()),
      static_cast<std::streamsize>(bytes));
    CHECK_F(stream.good());
    return std::make_shared<ShaderByteCode<std::vector<std::uint32_t>>>(
      std::move(code));
  }

  class ProbeGraphics final : public graphics::d3d12::Graphics {
  public:
    ProbeGraphics(const SerializedBackendConfig& config,
      const SerializedPathFinderConfig& paths)
      : Graphics(config, paths)
      , probe_(LoadProbe())
    {
    }

    auto GetShader(const ShaderRequest& request) const
      -> std::shared_ptr<IShaderByteCode> override
    {
      if (request.source_path == "Tests/LightingGpuAbiProbe.hlsl") {
        CHECK_F(request.stage == ShaderType::kCompute
          && request.entry_point == "CS" && request.defines.empty());
        return probe_;
      }
      return Graphics::GetShader(request);
    }

  private:
    std::shared_ptr<IShaderByteCode> probe_;
  };

  auto ProbeRootBindings() -> std::vector<RootBindingItem>
  {
    auto bindings = std::vector<RootBindingItem> {};
    for (const auto& parameter : root::kRootParamTable) {
      auto binding = RootBindingDesc {};
      binding.binding_slot_desc.register_index = parameter.shader_register;
      binding.binding_slot_desc.register_space = parameter.register_space;
      binding.visibility = ShaderStageFlags::kAll;
      if (parameter.kind == root::RootParamKind::DescriptorTable) {
        const auto& range = parameter.ranges.front();
        CHECK_F(range.range_type == root::RangeType::SRV
          || range.range_type == root::RangeType::Sampler);
        binding.data = DescriptorTableBinding {
          .view_type = range.range_type == root::RangeType::Sampler
            ? ResourceViewType::kSampler
            : ResourceViewType::kRawBuffer_SRV,
          .base_index = range.base_register,
          .count = range.num_descriptors,
        };
      } else if (parameter.kind == root::RootParamKind::CBV) {
        binding.data = DirectBufferBinding {};
      } else {
        binding.data
          = PushConstantsBinding { .size = parameter.constants_count };
      }
      bindings.emplace_back(binding);
    }
    return bindings;
  }

} // namespace

auto LightingGpuAbiTest::CreateBackend(const SerializedBackendConfig& config,
  const SerializedPathFinderConfig& paths)
  -> std::shared_ptr<graphics::d3d12::Graphics>
{
  return std::make_shared<ProbeGraphics>(config, paths);
}

auto LightingGpuAbiTest::BackendConfigJson() const -> std::string
{
  return R"({"enable_debug_layer":true})";
}

auto LightingGpuAbiTest::PathFinderConfigJson() const -> std::string
{
  return R"({"workspace_root_path":")" OXYGEN_LIGHTING_ABI_WORKSPACE R"("})";
}

auto LightingGpuAbiTest::Decode(const DecodeRequest& request)
  -> std::vector<std::uint32_t>
{
  const auto& [records, stride, record_kind, decoded_words, first_element,
    count, indices_srv, constant_buffer_records] = request;
  CHECK_GT_F(stride, 0U);
  CHECK_EQ_F(records.size_bytes() % stride, 0U);
  CHECK_LE_F(static_cast<std::uint64_t>(first_element) + count,
    records.size_bytes() / stride);
  CHECK_GT_F(count, 0U);
  const auto output_bytes
    = static_cast<std::uint64_t>(count) * decoded_words * sizeof(std::uint32_t);
  const auto record_count = records.size_bytes() / stride;
  constexpr auto kAlignment = packing::kConstantBufferAlignment;
  const auto storage_stride = constant_buffer_records
    ? (static_cast<std::uint64_t>(stride) + kAlignment - 1U) / kAlignment
      * kAlignment
    : stride;
  auto input = CreateRegisteredBuffer({
    .size_bytes = record_count * storage_stride,
    .usage
    = constant_buffer_records ? BufferUsage::kConstant : BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Lighting ABI records",
  });
  for (std::size_t index = 0; index < record_count; ++index) {
    const auto record = records.subspan(index * stride, stride);
    input->Update(record.data(), record.size_bytes(), index * storage_stride);
  }
  auto output = CreateRegisteredBuffer({
    .size_bytes = output_bytes,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = "Lighting ABI decoded",
  });
  oxygen::Graphics& graphics_api = Backend();
  auto& allocator = graphics_api.GetDescriptorAllocator();
  auto& registry = Backend().GetResourceRegistry();
  auto source = input;
  auto source_size = records.size_bytes();
  auto source_stride = stride;
  if (constant_buffer_records) {
    auto cbv_slots = std::vector<ShaderVisibleIndex> {};
    cbv_slots.reserve(record_count);
    for (std::size_t index = 0; index < record_count; ++index) {
      auto handle = allocator.AllocateRaw(ResourceViewType::kConstantBuffer,
        DescriptorVisibility::kShaderVisible);
      cbv_slots.push_back(allocator.GetShaderVisibleIndex(handle));
      registry.RegisterView(*input, std::move(handle),
        BufferViewDescription {
          .view_type = ResourceViewType::kConstantBuffer,
          .range = { index * storage_stride, storage_stride },
        });
    }
    source_size = cbv_slots.size() * sizeof(ShaderVisibleIndex);
    source_stride = sizeof(ShaderVisibleIndex);
    source = CreateRegisteredBuffer({
      .size_bytes = source_size,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = "Lighting ABI CBV indices",
    });
    source->Update(cbv_slots.data(), source_size, 0U);
  }
  auto input_handle
    = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
  const auto input_slot = allocator.GetShaderVisibleIndex(input_handle);
  registry.RegisterView(*source, std::move(input_handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .range = { 0U, source_size },
      .stride = source_stride,
    });
  auto output_handle = allocator.AllocateRaw(
    ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(output_handle);
  registry.RegisterView(*output, std::move(output_handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kRawBuffer_UAV,
      .range = { 0U, output_bytes },
      .stride = 0U,
    });
  const auto arguments = std::array {
    input_slot.get(),
    output_slot.get(),
    decoded_words,
    first_element,
    indices_srv.get(),
    0U,
    0U,
    0U,
  };
  auto constants = CreateRegisteredBuffer({
    .size_bytes = sizeof(arguments),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Lighting ABI arguments",
  });
  constants->Update(arguments.data(), sizeof(arguments), 0U);
  auto constants_handle
    = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
  const auto constants_slot = allocator.GetShaderVisibleIndex(constants_handle);
  registry.RegisterView(*constants, std::move(constants_handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .range = { 0U, sizeof(arguments) },
      .stride = sizeof(arguments),
    });
  const auto pipeline = ComputePipelineDesc::Builder {}
                          .SetComputeShader({
                            .stage = ShaderType::kCompute,
                            .source_path = "Tests/LightingGpuAbiProbe.hlsl",
                            .entry_point = "CS",
                          })
                          .SetRootBindings(ProbeRootBindings())
                          .SetDebugName("Lighting ABI decode")
                          .Build();
  auto readback
    = GetReadbackManager()->CreateBufferReadback("Lighting ABI result");
  SubmitCommands("Lighting ABI upload/decode/readback",
    [&](CommandRecorder& recorder) -> void {
      EnsureTracked(recorder, input, ResourceStates::kGenericRead);
      if (source != input) {
        EnsureTracked(recorder, source, ResourceStates::kGenericRead);
      }
      EnsureTracked(recorder, constants, ResourceStates::kGenericRead);
      if (indices_buffer_) {
        EnsureTracked(recorder, indices_buffer_, ResourceStates::kGenericRead);
      }
      EnsureTracked(recorder, output, ResourceStates::kCommon);
      recorder.RequireResourceState(*output, ResourceStates::kUnorderedAccess);
      recorder.FlushBarriers();
      recorder.SetPipelineState(pipeline);
      recorder.SetComputeRoot32BitConstant(
        static_cast<std::uint32_t>(root::RootParam::kRootConstants),
        record_kind, 0U);
      recorder.SetComputeRoot32BitConstant(
        static_cast<std::uint32_t>(root::RootParam::kRootConstants),
        constants_slot.get(), 1U);
      recorder.Dispatch(count, 1U, 1U);
      CHECK_F(readback->EnqueueCopy(recorder, *output, { 0U, output_bytes })
          .has_value());
    });
  const auto mapped = readback->MapNow();
  CHECK_F(mapped.has_value());
  CHECK_EQ_F(mapped->Bytes().size_bytes(), output_bytes);
  auto result = std::vector<std::uint32_t>(
    static_cast<std::size_t>(count) * decoded_words);
  std::memcpy(
    result.data(), mapped->Bytes().data(), mapped->Bytes().size_bytes());
  return result;
}

auto LightingGpuAbiTest::PublishIndices(std::span<const std::uint32_t> indices)
  -> ShaderVisibleIndex
{
  CHECK_F(!indices.empty());
  indices_buffer_ = CreateRegisteredBuffer({
    .size_bytes = indices.size_bytes(),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Lighting probe compact indices",
  });
  indices_buffer_->Update(indices.data(), indices.size_bytes(), 0U);
  oxygen::Graphics& graphics_api = Backend();
  auto& allocator = graphics_api.GetDescriptorAllocator();
  auto handle
    = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
  const auto slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*indices_buffer_,
    std::move(handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .range = { 0U, indices.size_bytes() },
      .stride = sizeof(std::uint32_t),
    });
  return slot;
}

auto LightingGpuAbiTest::PublishPackedTexture(const Format format,
  const std::span<const std::uint32_t> texels) -> ShaderVisibleIndex
{
  CHECK_F(!texels.empty());
  CHECK_LE_F(texels.size(), std::numeric_limits<std::uint32_t>::max());
  CHECK_F(format == Format::kRGBA8UNorm || format == Format::kRGBA8UNormSRGB
    || format == Format::kR10G10B10A2UNorm);
  auto texture = CreateRegisteredTexture({
    .width = static_cast<std::uint32_t>(texels.size()),
    .height = 1U,
    .format = format,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "Lighting material decode texture",
    .is_shader_resource = true,
  });
  const auto slice = graphics::TextureSlice {
    .width = static_cast<std::uint32_t>(texels.size()),
    .height = 1U,
    .depth = 1U,
  };
  const auto footprint
    = graphics::ComputeLinearTextureCopyFootprint(texture->GetDescriptor(),
      slice, SizeBytes { D3D12_TEXTURE_DATA_PITCH_ALIGNMENT });
  auto upload = CreateRegisteredBuffer({
    .size_bytes = footprint.total_bytes.get(),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Lighting material decode upload",
  });
  upload->Update(texels.data(), texels.size_bytes(), 0U);
  SubmitCommands("Lighting material texture upload",
    [&](graphics::CommandRecorder& recorder) -> void {
      EnsureTracked(recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(recorder, texture, ResourceStates::kCommon);
      recorder.RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder.FlushBarriers();
      recorder.CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = footprint.row_pitch.get(),
          .buffer_slice_pitch = footprint.slice_pitch.get(),
          .dst_slice = slice,
        },
        *texture);
      recorder.RequireResourceState(*texture, ResourceStates::kShaderResource);
      recorder.FlushBarriers();
    });
  oxygen::Graphics& graphics_api = Backend();
  auto& allocator = graphics_api.GetDescriptorAllocator();
  auto handle = allocator.AllocateBindless(
    bindless::generated::kGlobalSrvDomain, ResourceViewType::kTexture_SRV);
  CHECK_F(handle.IsValid());
  const auto index = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
    graphics::TextureViewDescription {
      .format = format,
      .dimension = TextureType::kTexture2D,
    });
  return index;
}

auto LightingGpuAbiTest::PublishBrdfMomentTextures()
  -> std::array<ShaderVisibleIndex, 2>
{
  const auto model = lighting::internal::GetBrdfMomentData();
  CHECK_F(model.has_value());
  const auto sources = std::array { model->moments, model->means };
  const auto widths = std::array { model->view_nodes, 1U };
  auto slots = std::array<ShaderVisibleIndex, 2> {};
  for (std::size_t index = 0; index < slots.size(); ++index) {
    auto texture = CreateRegisteredTexture({
      .width = widths.at(index),
      .height = model->roughness_nodes,
      .format = Format::kRG32Float,
      .debug_name = "BRDF moment probe",
      .is_shader_resource = true,
    });
    const auto slice = graphics::TextureSlice {
      .width = widths.at(index),
      .height = model->roughness_nodes,
      .depth = 1U,
    };
    const auto footprint
      = graphics::ComputeLinearTextureCopyFootprint(texture->GetDescriptor(),
        slice, SizeBytes { D3D12_TEXTURE_DATA_PITCH_ALIGNMENT });
    auto upload = CreateRegisteredBuffer({
      .size_bytes = footprint.total_bytes.get(),
      .memory = BufferMemory::kUpload,
      .debug_name = "BRDF moment probe upload",
    });
    constexpr auto kPairBytes = sizeof(float) * 2U;
    const auto source_pitch
      = static_cast<std::size_t>(widths.at(index)) * kPairBytes;
    for (std::uint32_t row = 0; row < model->roughness_nodes; ++row) {
      upload->Update(
        sources.at(index).subspan(row * source_pitch, source_pitch).data(),
        source_pitch,
        static_cast<std::uint64_t>(row) * footprint.row_pitch.get());
    }
    SubmitCommands("BRDF moment probe upload",
      [&](graphics::CommandRecorder& recorder) -> void {
        EnsureTracked(recorder, upload, ResourceStates::kGenericRead);
        EnsureTracked(recorder, texture, ResourceStates::kCommon);
        recorder.RequireResourceState(*texture, ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        recorder.CopyBufferToTexture(*upload,
          {
            .buffer_offset = 0U,
            .buffer_row_pitch = footprint.row_pitch.get(),
            .buffer_slice_pitch = footprint.slice_pitch.get(),
            .dst_slice = slice,
          },
          *texture);
        recorder.RequireResourceState(
          *texture, ResourceStates::kShaderResource);
        recorder.FlushBarriers();
      });
    oxygen::Graphics& api = Backend();
    auto& allocator = api.GetDescriptorAllocator();
    auto handle = allocator.AllocateBindless(
      bindless::generated::kTexturesDomain, ResourceViewType::kTexture_SRV);
    CHECK_F(handle.IsValid());
    slots.at(index) = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
      graphics::TextureViewDescription {
        .format = Format::kRG32Float,
        .dimension = TextureType::kTexture2D,
      });
  }
  return slots;
}

auto LightingGpuAbiTest::TearDown() -> void
{
  indices_buffer_.reset();
  ReadbackTestFixture::TearDown();
}

} // namespace oxygen::vortex::testing
