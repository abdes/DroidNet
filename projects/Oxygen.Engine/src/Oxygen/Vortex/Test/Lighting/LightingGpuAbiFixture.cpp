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
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
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
    count] = request;
  CHECK_GT_F(stride, 0U);
  CHECK_EQ_F(records.size_bytes() % stride, 0U);
  CHECK_LE_F(static_cast<std::uint64_t>(first_element) + count,
    records.size_bytes() / stride);
  CHECK_GT_F(count, 0U);
  const auto output_bytes
    = static_cast<std::uint64_t>(count) * decoded_words * sizeof(std::uint32_t);
  auto input = CreateRegisteredBuffer({
    .size_bytes = records.size_bytes(),
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "Lighting ABI records",
  });
  input->Update(records.data(), records.size_bytes(), 0U);
  auto output = CreateRegisteredBuffer({
    .size_bytes = output_bytes,
    .usage = BufferUsage::kStorage,
    .memory = BufferMemory::kDeviceLocal,
    .debug_name = "Lighting ABI decoded",
  });
  oxygen::Graphics& graphics_api = Backend();
  auto& allocator = graphics_api.GetDescriptorAllocator();
  auto& registry = Backend().GetResourceRegistry();
  auto input_handle
    = allocator.AllocateBindless(bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
  const auto input_slot = allocator.GetShaderVisibleIndex(input_handle);
  registry.RegisterView(*input, std::move(input_handle),
    BufferViewDescription {
      .view_type = ResourceViewType::kStructuredBuffer_SRV,
      .range = { 0U, records.size_bytes() },
      .stride = stride,
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
      EnsureTracked(recorder, constants, ResourceStates::kGenericRead);
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

} // namespace oxygen::vortex::testing
