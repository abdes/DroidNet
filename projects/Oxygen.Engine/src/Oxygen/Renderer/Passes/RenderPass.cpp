//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Renderer/Internal/RenderScope.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

using oxygen::engine::RenderPass;
using oxygen::graphics::CommandRecorder;

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  DLOG_SCOPE_F(2, "RenderPass PrepareResources");
  DLOG_F(2, "pass: {}", GetName());

  ValidateConfig();

  // Check if we need to rebuild the pipeline state and the root signature.
  if (NeedRebuildPipelineState()) {
    last_built_pso_desc_ = CreatePipelineStateDesc();
  }

  co_await DoPrepareResources(recorder);

  co_return;
}

auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  DCHECK_F(!NeedRebuildPipelineState()); // built in PrepareResources

  detail::RenderScope ctx_scope(context_, context);

  DLOG_SCOPE_F(2, "RenderPass Execute");
  DLOG_F(2, "pass: {}", GetName());

  // This will try to get a cached pipeline state or create a new one if needed.
  // It also sets the bindless root signature.
  recorder.SetPipelineState(*last_built_pso_desc_);
  // Do these after the pipeline state is set.
  BindIndicesBuffer(recorder);
  BindSceneConstantsBuffer(recorder);

  try {
    co_await DoExecute(recorder);
  } catch (const std::exception& ex) {
    DLOG_F(ERROR, "{}: Execute failed: {}", GetName(), ex.what());
    throw; // Re-throw to propagate the error
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

auto RenderPass::BindSceneConstantsBuffer(CommandRecorder& recorder) const
  -> void
{
  using graphics::DirectBufferBinding;

  DCHECK_NOTNULL_F(Context().scene_constants);
  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      binding::RootParam::kSceneConstants);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<DirectBufferBinding>(root_param.data),
    "Expected root parameter {}'s data to be DirectBufferBinding",
    root_param_index);

  // Bind the buffer as a root CBV (direct GPU virtual address)
  recorder.SetGraphicsRootConstantBufferView(
    root_param.GetRootParameterIndex(), // should be binding 2 (b1, space0)
    Context().scene_constants->GetGPUVirtualAddress());
}

auto RenderPass::BindIndicesBuffer(CommandRecorder& recorder) const -> void
{
  // In the bindless rendering model, the indices buffer (DrawResourceIndices)
  // is already accessible through the descriptor table at heap index 0.
  // The shader accesses it via g_DrawResourceIndices[0] in space0.
  // No additional binding is required here.
  (void)recorder; // Suppress unused parameter warning
}

auto RenderPass::BindDrawIndexConstant(
  CommandRecorder& recorder, uint32_t draw_index) const -> void
{
  using graphics::PushConstantsBinding;

  DCHECK_F(LastBuiltPsoDesc().has_value());

  constexpr auto root_param_index
    = static_cast<std::span<const graphics::RootBindingItem>::size_type>(
      binding::RootParam::kDrawIndex);
  const auto& root_param = LastBuiltPsoDesc()->RootBindings()[root_param_index];

  DCHECK_F(std::holds_alternative<PushConstantsBinding>(root_param.data),
    "Expected root parameter {}'s data to be PushConstantsBinding",
    root_param_index);

  // Bind the draw index as a root constant (32-bit value)
  recorder.SetGraphicsRoot32BitConstant(
    root_param.GetRootParameterIndex(), // should be binding 3 for draw index
    draw_index,
    0); // offset within the constant (0 for single 32-bit value)
}

auto RenderPass::IssueDrawCallsOverPass(
  CommandRecorder& recorder, PassMaskBit pass_bit) const noexcept -> void
{
  try {
    const auto psf = Context().prepared_frame;
    if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
      return; // nothing to do
    }

    const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
      psf->draw_metadata_bytes.data());

    uint32_t emitted_count = 0;
    uint32_t skipped_invalid = 0;
    uint32_t draw_errors = 0;

    if (psf->partitions.empty()) {
      // Defensive fallback: if partitions missing, iterate all and apply pass
      // bit to preserve correctness.
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
    // Catch-all to honor noexcept: log and return
    DLOG_F(ERROR, "RenderPass '{}' IssueDrawCallsOverPass failed: {}",
      GetName(), ex.what());
  } catch (...) {
    DLOG_F(ERROR,
      "RenderPass '{}' IssueDrawCallsOverPass failed: unknown error",
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
    // Runtime guards (release-safe): skip invalid metadata
    if ((md.is_indexed && md.index_count == 0)
      || (!md.is_indexed && md.vertex_count == 0)) {
      ++skipped_invalid;
      continue;
    }
    try {
      BindDrawIndexConstant(recorder, draw_index);
      recorder.Draw(md.is_indexed ? md.index_count : md.vertex_count, 1, 0, 0);
      ++emitted_count;
    } catch (const std::exception& ex) {
      ++draw_errors;
      DLOG_F(ERROR, "RenderPass '{}' draw_index={} failed: {}. Draw dropped.",
        GetName(), draw_index, ex.what());
      continue;
    } catch (...) {
      ++draw_errors;
      DLOG_F(ERROR,
        "RenderPass '{}' draw_index={} failed: unknown error. Draw dropped.",
        GetName(), draw_index);
      continue;
    }
  }
}

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
      // If ranges present, use first range to select a representative view
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
