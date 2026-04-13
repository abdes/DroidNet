//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/RenderScope.h>
#include <Oxygen/Vortex/Passes/RenderPass.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

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
    const b::RootParamDesc& desc = b::kRootParamTable[i];
    g::RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = desc.shader_register;
    binding.binding_slot_desc.register_space = desc.register_space;
    binding.visibility = g::ShaderStageFlags::kAll;

    switch (desc.kind) {
    case b::RootParamKind::DescriptorTable: {
      if (desc.ranges_count > 0 && desc.ranges.data() != nullptr) {
        const b::RootParamRange& range = desc.ranges[0];
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

  const auto& desc = b::kRootParamTable[static_cast<std::size_t>(
    b::RootParam::kRootConstants)];
  return graphics::BindingSlotDesc {
    .register_index = desc.shader_register,
    .register_space = desc.register_space,
  };
}

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope context_scope(context_, context);

  const auto scope_options = Context().GetRenderer().MakeGpuEventScopeOptions();
  auto marker_scope_options = scope_options;
  marker_scope_options.timestamp_enabled = false;
  graphics::GpuEventScope pass_scope(recorder, GetName(), marker_scope_options);
  graphics::GpuEventScope phase_scope(
    recorder, "PrepareResources", marker_scope_options);

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

auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope context_scope(context_, context);

  const auto scope_options = Context().GetRenderer().MakeGpuEventScopeOptions();
  graphics::GpuEventScope pass_scope(recorder, GetName(), scope_options);
  auto marker_scope_options = scope_options;
  marker_scope_options.timestamp_enabled = false;
  graphics::GpuEventScope phase_scope(
    recorder, "Execute", marker_scope_options);

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

auto RenderPass::BindDrawIndexConstant(
  CommandRecorder& recorder, const std::uint32_t draw_index) const -> void
{
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
    draw_index, 0);
}

auto RenderPass::IssueDrawCallsOverPass(
  CommandRecorder& recorder, const PassMaskBit pass_bit) const noexcept -> void
{
  try {
    const auto prepared = Context().current_view.prepared_frame;
    if (!prepared || !prepared->IsValid()
      || prepared->draw_metadata_bytes.empty()) {
      return;
    }

    const auto* records = reinterpret_cast<const oxygen::vortex::DrawMetadata*>(
      prepared->draw_metadata_bytes.data());

    uint32_t emitted_count = 0;
    uint32_t skipped_invalid = 0;
    uint32_t draw_errors = 0;

    if (prepared->partitions.empty()) {
      const auto count = prepared->draw_metadata_bytes.size()
        / sizeof(oxygen::vortex::DrawMetadata);
      for (uint32_t i = 0; i < count; ++i) {
        if (!records[i].flags.IsSet(pass_bit)) {
          continue;
        }
        EmitDrawRange(recorder, records, i, i + 1, emitted_count,
          skipped_invalid, draw_errors);
      }
      return;
    }

    for (const auto& partition : prepared->partitions) {
      if (partition.pass_mask.IsSet(pass_bit)) {
        EmitDrawRange(recorder, records, partition.begin, partition.end,
          emitted_count, skipped_invalid, draw_errors);
      }
    }
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "RenderPass '{}' IssueDrawCallsOverPass failed: {}", GetName(),
      ex.what());
  } catch (...) {
    LOG_F(ERROR, "RenderPass '{}' IssueDrawCallsOverPass failed: unknown error",
      GetName());
  }
}

auto RenderPass::EmitDrawRange(CommandRecorder& recorder,
  const DrawMetadata* records, const uint32_t begin, const uint32_t end,
  uint32_t& emitted_count, uint32_t& skipped_invalid,
  uint32_t& draw_errors) const noexcept -> void
{
  for (uint32_t draw_index = begin; draw_index < end; ++draw_index) {
    const auto& metadata = records[draw_index];
    if ((metadata.is_indexed && metadata.index_count == 0)
      || (!metadata.is_indexed && metadata.vertex_count == 0)) {
      ++skipped_invalid;
      continue;
    }

    try {
      BindDrawIndexConstant(recorder, draw_index);
      recorder.Draw(
        metadata.is_indexed ? metadata.index_count : metadata.vertex_count,
        metadata.instance_count, 0, 0);
      ++emitted_count;
    } catch (const std::exception& ex) {
      ++draw_errors;
      LOG_F(ERROR, "RenderPass '{}' draw_index={} failed: {}. Draw dropped.",
        GetName(), draw_index, ex.what());
    } catch (...) {
      ++draw_errors;
      LOG_F(ERROR,
        "RenderPass '{}' draw_index={} failed: unknown error. Draw dropped.",
        GetName(), draw_index);
    }
  }
}
