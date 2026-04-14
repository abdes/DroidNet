//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Oxygen/Composition/ObjectMetadata.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Renderer/Internal/RenderScope.h>
#include <Oxygen/Renderer/Passes/RenderPass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>

using oxygen::engine::RenderPass;
using oxygen::graphics::CommandRecorder;

namespace {
namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

auto RangeTypeToViewType(bindless_d3d12::RangeType rt)
  -> oxygen::graphics::ResourceViewType
{
  using oxygen::graphics::ResourceViewType;

  switch (rt) {
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

/*!
 Translates the statically generated `kRootParamTable` into the
 `RootBindingItem` list consumed by the D3D12/Vulkan pipeline builder.

 Both graphics and compute pipelines must use the same layout so that shader
 ABI requirements (e.g., `ViewConstants` at `b1`, `RootConstants` at `b2`)
 are satisfied.  The function mirrors every entry in `kRootParamTable`
 one-to-one, converting engine-side `RootParamKind` variants to their
 graphics-layer equivalents.

 @return Vector of `RootBindingItem` matching the bindless root signature
         generated from `Bindless.yaml`.
 @see BuildRootBindings (declaration), binding::kRootParamTable
*/
auto RenderPass::BuildRootBindings() -> std::vector<graphics::RootBindingItem>
{
  namespace g = oxygen::graphics;
  namespace b = oxygen::bindless::generated::d3d12;

  std::vector<g::RootBindingItem> out;
  out.reserve(b::kRootParamTableCount);

  for (uint32_t i = 0; i < b::kRootParamTableCount; ++i) {
    const b::RootParamDesc& d = b::kRootParamTable[i];
    g::RootBindingDesc desc {};
    desc.binding_slot_desc.register_index = d.shader_register;
    desc.binding_slot_desc.register_space = d.register_space;
    desc.visibility = g::ShaderStageFlags::kAll;

    switch (d.kind) {
    case b::RootParamKind::DescriptorTable: {
      if (d.ranges_count > 0 && d.ranges.data() != nullptr) {
        const b::RootParamRange& r = d.ranges[0];
        g::DescriptorTableBinding table {};
        table.view_type
          = RangeTypeToViewType(static_cast<b::RangeType>(r.range_type));
        table.base_index = r.base_register;
        if (r.num_descriptors == std::numeric_limits<uint32_t>::max()) {
          table.count = (std::numeric_limits<uint32_t>::max)();
        } else {
          table.count = r.num_descriptors;
        }
        desc.data = table;
      } else {
        g::DescriptorTableBinding table {};
        table.view_type = g::ResourceViewType::kNone;
        table.base_index = 0;
        table.count = (std::numeric_limits<uint32_t>::max)();
        desc.data = table;
      }
      break;
    }
    case b::RootParamKind::CBV: {
      desc.data = g::DirectBufferBinding {};
      break;
    }
    case b::RootParamKind::RootConstants: {
      g::PushConstantsBinding pc {};
      pc.size = d.constants_count;
      desc.data = pc;
      break;
    }
    }

    out.emplace_back(desc);
  }

  return out;
}

auto RenderPass::RootConstantsBindingSlot() -> graphics::BindingSlotDesc
{
  namespace b = oxygen::bindless::generated::d3d12;

  const auto& root_constants_desc = b::kRootParamTable[static_cast<std::size_t>(
    b::RootParam::kRootConstants)];
  return graphics::BindingSlotDesc {
    .register_index = root_constants_desc.shader_register,
    .register_space = root_constants_desc.register_space,
  };
}

/*!
 Registers an `ObjectMetadata` component to store the pass name.

 @param name Human-readable name used for GPU event scopes and logging.
*/
RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetadata>(name);
}

/*!
 Sets the active render context, opens GPU event and marker scopes around the
 work, calls `ValidateConfig`, delegates PSO rebuild to the derived base class
 via `OnPrepareResources`, then `co_await`s `DoPrepareResources`.

 Any exception thrown by `DoPrepareResources` is logged and re-thrown so the
 pipeline scheduler can surface it as a frame-level error.

 @param context Render context owning the current view and renderer handle.
 @param recorder Command recorder for resource transitions and barriers.
 @return Coroutine that completes when all resources are ready.
 @throw Propagates any exception thrown by `DoPrepareResources`.
 @see Execute, DoPrepareResources, OnPrepareResources
*/
auto RenderPass::PrepareResources(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  graphics::GpuEventScope pass_scope(recorder, GetName(),
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  graphics::GpuEventScope phase_scope(recorder, "RenderPass.PrepareResources",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass,
    profiling::Vars(profiling::Var("pass", GetName())));

  DLOG_SCOPE_F(2, "RenderPass PrepareResources");
  DLOG_F(2, "pass: {}", GetName());

  ValidateConfig();

  // Let derived base class (Graphics/Compute) handle PSO rebuild
  OnPrepareResources(recorder);

  try {
    co_await DoPrepareResources(recorder);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "{}: PrepareResources failed: {}", GetName(), ex.what());
    throw;
  }

  co_return;
}

/*!
 Sets the active render context, opens a timestamped GPU event scope around the
 pass, delegates pipeline-state setup to the derived base class via `OnExecute`,
 then `co_await`s `DoExecute`.

 Any exception thrown by `DoExecute` is logged and re-thrown so the pipeline
 scheduler can surface it as a frame-level error.

 @param context Render context owning the current view and renderer handle.
 @param recorder Command recorder for issuing draw or dispatch commands.
 @return Coroutine that completes when all commands for this pass are recorded.
 @throw Propagates any exception thrown by `DoExecute`.

 @see PrepareResources, DoExecute, OnExecute
*/
auto RenderPass::Execute(
  const RenderContext& context, CommandRecorder& recorder) -> co::Co<>
{
  detail::RenderScope ctx_scope(context_, context);

  graphics::GpuEventScope pass_scope(recorder, GetName(),
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  graphics::GpuEventScope phase_scope(recorder, "RenderPass.Execute",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass,
    profiling::Vars(profiling::Var("pass", GetName())));

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

/*!
 @return Name stored in the `ObjectMetadata` component.
*/
auto RenderPass::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetadata>().GetName();
}

/*!
 @param name New name forwarded to the `ObjectMetadata` component.
*/
auto RenderPass::SetName(std::string_view name) noexcept -> void
{
  GetComponent<ObjectMetadata>().SetName(name);
}

/*!
 Asserts that a context has been set (i.e., the pass is inside an active
 `PrepareResources` or `Execute` coroutine).

 @return Reference to the render context injected by the pipeline scheduler.
 @warning Calling this outside of a pass execution coroutine is undefined
          behaviour; the internal `DCHECK_NOTNULL_F` guards against it in
          debug builds only.
*/
auto RenderPass::Context() const -> const RenderContext&
{
  DCHECK_NOTNULL_F(context_);
  return *context_;
}

/*!
 Writes `draw_index` into DWORD0 of the `kRootConstants` root parameter so
 the vertex shader can fetch per-draw data from the bindless buffer.

 @param recorder Command recorder receiving the root-constant write.
 @param draw_index Zero-based index into the `PreparedSceneFrame` draw
        metadata array.
 @see EmitDrawRange, IssueDrawCallsOverPass
*/
auto RenderPass::BindDrawIndexConstant(
  CommandRecorder& recorder, DrawIndex draw_index) const -> void
{
  // Bind the draw index root constant (first 32-bit value)
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(
      oxygen::bindless::generated::d3d12::RootParam::kRootConstants),
    draw_index.get(), 0);
}

/*!
 Iterates `PreparedSceneFrame::partitions`. For each partition whose
 `pass_mask` includes `pass_bit`, delegates to `EmitDrawRange`. When no
 partitions exist the full draw list is scanned and each record is filtered
 individually by its per-draw flags.

 All exceptions are caught and logged; the method is `noexcept` so a single
 failing draw cannot abort the frame.

 @param recorder Command recorder receiving the draw calls.
 @param pass_bit The pass-mask bit that a partition must include to be drawn.

 @see EmitDrawRange, BindDrawIndexConstant
*/
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

/*!
 Iterates draw records in `[begin, end)`. Records with a zero primitive count
 are skipped and counted in `skipped_invalid`. For each valid record,
 `BindDrawIndexConstant` is called followed by `CommandRecorder::Draw`. Each
 individual draw is wrapped in a try/catch so one failing call does not
 prevent the rest of the range from being emitted.

 @param recorder Command recorder receiving the draw calls.
 @param records Pointer to the start of the draw-metadata array.
 @param begin First draw index to process (inclusive).
 @param end One past the last draw index to process.
 @param emitted_count Incremented for each successfully emitted draw.
 @param skipped_invalid Incremented for each record with a zero count.
 @param draw_errors Incremented for each draw that threw an exception.

 @see IssueDrawCallsOverPass, BindDrawIndexConstant
*/
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
