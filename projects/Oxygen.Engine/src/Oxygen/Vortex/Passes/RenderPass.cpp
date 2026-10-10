//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/RenderScope.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

using oxygen::graphics::CommandRecorder;
using oxygen::vortex::RenderPass;

namespace {
namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

auto RangeTypeToViewType(bindless_d3d12::RangeType type)
  -> oxygen::graphics::ResourceViewType
{
  using oxygen::graphics::ResourceViewType;

  switch (type) {
  case bindless_d3d12::RangeType::SRV:
    return ResourceViewType::kRawBuffer_SRV;
  case bindless_d3d12::RangeType::Sampler:
    return ResourceViewType::kSampler;
  case bindless_d3d12::RangeType::UAV:
    return ResourceViewType::kRawBuffer_UAV;
  default:
    return ResourceViewType::kNone;
  }
}
} // namespace

auto RenderPass::BuildRootBindings() -> std::vector<graphics::RootBindingItem>
{
  namespace b = oxygen::bindless::generated::d3d12;
  namespace g = oxygen::graphics;

  std::vector<g::RootBindingItem> out;
  out.reserve(b::kRootParamTableCount);

  for (uint32_t i = 0; i < b::kRootParamTableCount; ++i) {
    const b::RootParamDesc& desc = b::kRootParamTable.at(i);
    g::RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = desc.shader_register;
    binding.binding_slot_desc.register_space = desc.register_space;
    binding.visibility = g::ShaderStageFlags::kAll;

    switch (desc.kind) {
    case b::RootParamKind::DescriptorTable: {
      if (desc.ranges_count > 0 && desc.ranges.data() != nullptr) {
        const b::RootParamRange& range = desc.ranges.front();
        g::DescriptorTableBinding table {};
        table.view_type
          = RangeTypeToViewType(static_cast<b::RangeType>(range.range_type));
        table.base_index = range.base_register;
        table.count
          = range.num_descriptors == std::numeric_limits<uint32_t>::max()
          ? (std::numeric_limits<uint32_t>::max)()
          : range.num_descriptors;
        binding.data = table;
      } else {
        g::DescriptorTableBinding table {};
        table.view_type = g::ResourceViewType::kNone;
        table.base_index = 0;
        table.count = (std::numeric_limits<uint32_t>::max)();
        binding.data = table;
      }
      break;
    }
    case b::RootParamKind::CBV:
      binding.data = g::DirectBufferBinding {};
      break;
    case b::RootParamKind::RootConstants: {
      g::PushConstantsBinding constants {};
      constants.size = desc.constants_count;
      binding.data = constants;
      break;
    }
    }

    out.emplace_back(binding);
  }

  return out;
}

auto RenderPass::RootConstantsBindingSlot() -> graphics::BindingSlotDesc
{
  namespace b = oxygen::bindless::generated::d3d12;

  const auto& desc = b::kRootParamTable.at(
    static_cast<std::size_t>(b::RootParam::kRootConstants));
  return graphics::BindingSlotDesc {
    .register_index = desc.shader_register,
    .register_space = desc.register_space,
  };
}

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope context_scope(context_, context);

  graphics::GpuEventScope pass_scope(recorder, GetName(),
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  graphics::GpuEventScope phase_scope(recorder,
    "Vortex.RenderPass.PrepareResources",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass,
    profiling::Vars(profiling::Var("pass", GetName())));

  DLOG_SCOPE_F(2, "RenderPass PrepareResources");
  DLOG_F(2, "pass: {}", GetName());

  ValidateConfig();
  OnPrepareResources(recorder);

  try {
    co_await DoPrepareResources(recorder);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: PrepareResources failed: {}", GetName(), ex.what());
    throw;
  }

  co_return;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope context_scope(context_, context);

  graphics::GpuEventScope pass_scope(recorder, GetName(),
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  graphics::GpuEventScope phase_scope(recorder, "Vortex.RenderPass.Execute",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass,
    profiling::Vars(profiling::Var("pass", GetName())));

  DLOG_SCOPE_F(2, "RenderPass Execute");
  DLOG_F(2, "pass: {}", GetName());

  OnExecute(recorder);

  try {
    co_await DoExecute(recorder);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: Execute failed: {}", GetName(), ex.what());
    throw;
  }

  co_return;
}

auto RenderPass::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

auto RenderPass::SetName(std::string_view name) noexcept -> void
{
  GetComponent<ObjectMetadata>().SetName(name);
}

auto RenderPass::Context() const -> const RenderContext&
{
  DCHECK_NOTNULL_F(context_);
  return *context_;
}
