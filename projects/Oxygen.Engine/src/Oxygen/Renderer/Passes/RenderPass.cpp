//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ObjectMetadata.h>
#include <limits>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Renderer/Internal/RenderScope.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

using oxygen::engine::RenderPass;
using oxygen::graphics::CommandRecorder;

namespace {
auto RangeTypeToViewType(oxygen::engine::binding::RangeType rt)
  -> oxygen::graphics::ResourceViewType
{
  using oxygen::graphics::ResourceViewType;

  switch (rt) {
  case oxygen::engine::binding::RangeType::SRV:
    return ResourceViewType::kRawBuffer_SRV;
  case oxygen::engine::binding::RangeType::Sampler:
    return ResourceViewType::kSampler;
  case oxygen::engine::binding::RangeType::UAV:
    return ResourceViewType::kRawBuffer_UAV;
  default:
    return ResourceViewType::kNone;
  }
}
} // namespace

auto RenderPass::BuildRootBindings() -> std::vector<graphics::RootBindingItem>
{
  using namespace oxygen::graphics;
  using namespace oxygen::engine::binding;

  std::vector<RootBindingItem> out;
  out.reserve(kRootParamTableCount);

  for (uint32_t i = 0; i < kRootParamTableCount; ++i) {
    const RootParamDesc& d = kRootParamTable[i];
    RootBindingDesc desc {};
    desc.binding_slot_desc.register_index = d.shader_register;
    desc.binding_slot_desc.register_space = d.register_space;
    desc.visibility = ShaderStageFlags::kAll;

    switch (d.kind) {
    case RootParamKind::DescriptorTable: {
      if (d.ranges_count > 0 && d.ranges.data() != nullptr) {
        const RootParamRange& r = d.ranges[0];
        DescriptorTableBinding table {};
        table.view_type
          = RangeTypeToViewType(static_cast<RangeType>(r.range_type));
        table.base_index = r.base_register;
        if (r.num_descriptors == std::numeric_limits<uint32_t>::max()) {
          table.count = (std::numeric_limits<uint32_t>::max)();
        } else {
          table.count = r.num_descriptors;
        }
        desc.data = table;
      } else {
        DescriptorTableBinding table {};
        table.view_type = ResourceViewType::kNone;
        table.base_index = 0;
        table.count = (std::numeric_limits<uint32_t>::max)();
        desc.data = table;
      }
      break;
    }
    case RootParamKind::CBV: {
      desc.data = DirectBufferBinding {};
      break;
    }
    case RootParamKind::RootConstants: {
      PushConstantsBinding pc {};
      pc.size = d.constants_count;
      desc.data = pc;
      break;
    }
    }

    out.emplace_back(RootBindingItem(desc));
  }

  return out;
}

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  graphics::GpuEventScope phase_scope(recorder, "PrepareResources");
  graphics::GpuEventScope pass_scope(recorder, GetName());

  DLOG_SCOPE_F(2, "RenderPass PrepareResources");
  DLOG_F(2, "pass: {}", GetName());

  ValidateConfig();

  // Let derived base class (Graphics/Compute) handle PSO rebuild
  OnPrepareResources(recorder);

  co_await DoPrepareResources(recorder);

  co_return;
}

auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  graphics::GpuEventScope phase_scope(recorder, "Execute");
  graphics::GpuEventScope pass_scope(recorder, GetName());

  DLOG_SCOPE_F(2, "RenderPass Execute");
  DLOG_F(2, "pass: {}", GetName());

  // Let derived base class (Graphics/Compute) set pipeline state
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
  CommandRecorder& recorder, DrawIndex draw_index) const -> void
{
  // Bind the draw index root constant (first 32-bit value)
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), draw_index.get(),
    0);
}

auto RenderPass::IssueDrawCallsOverPass(
  CommandRecorder& recorder, PassMaskBit pass_bit) const noexcept -> void
{
  try {
    const auto psf = Context().current_view.prepared_frame;
    if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
      return;
    }

    const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
      psf->draw_metadata_bytes.data());

    uint32_t emitted_count = 0;
    uint32_t skipped_invalid = 0;
    uint32_t draw_errors = 0;

    if (psf->partitions.empty()) {
      const auto count
        = psf->draw_metadata_bytes.size() / sizeof(engine::DrawMetadata);
      for (uint32_t i = 0; i < count; ++i) {
        const auto& md = records[i];
        if (!md.flags.IsSet(pass_bit)) {
          continue;
        }
        EmitDrawRange(recorder, records, i, i + 1, emitted_count,
          skipped_invalid, draw_errors);
      }
      if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
        DLOG_F(2,
          "RenderPass '{}' pass {}: emitted={}, skipped_invalid={}, errors={} "
          "(no partitions)",
          GetName(), to_string(PassMask { pass_bit }), emitted_count,
          skipped_invalid, draw_errors);
      }
      return;
    }

    for (const auto& pr : psf->partitions) {
      if (pr.pass_mask.IsSet(pass_bit)) {
        EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
          skipped_invalid, draw_errors);
      }
    }
    if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
      DLOG_F(2,
        "RenderPass '{}' pass {}: emitted={}, skipped_invalid={}, errors={}",
        GetName(), to_string(PassMask { pass_bit }), emitted_count,
        skipped_invalid, draw_errors);
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
  const DrawMetadata* records, uint32_t begin, uint32_t end,
  uint32_t& emitted_count, uint32_t& skipped_invalid,
  uint32_t& draw_errors) const noexcept -> void
{
  for (uint32_t draw_index = begin; draw_index < end; ++draw_index) {
    const auto& md = records[draw_index];
    if ((md.is_indexed && md.index_count == 0)
      || (!md.is_indexed && md.vertex_count == 0)) {
      ++skipped_invalid;
      continue;
    }
    try {
      BindDrawIndexConstant(recorder, DrawIndex { draw_index });
      recorder.Draw(md.is_indexed ? md.index_count : md.vertex_count,
        md.instance_count, 0, 0);
      ++emitted_count;
    } catch (const std::exception& ex) {
      ++draw_errors;
      LOG_F(ERROR, "RenderPass '{}' draw_index={} failed: {}. Draw dropped.",
        GetName(), draw_index, ex.what());
      continue;
    } catch (...) {
      ++draw_errors;
      LOG_F(ERROR,
        "RenderPass '{}' draw_index={} failed: unknown error. Draw dropped.",
        GetName(), draw_index);
      continue;
    }
  }
}
