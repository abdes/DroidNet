//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

namespace oxygen::engine::internal {

using ValidationIssue = Renderer::ValidationIssue;
using ValidationReport = Renderer::ValidationReport;
using FrameSessionInput = Renderer::FrameSessionInput;
using OutputTargetInput = Renderer::OutputTargetInput;
using ResolvedViewInput = Renderer::ResolvedViewInput;
using PreparedFrameInput = Renderer::PreparedFrameInput;
using CoreShaderInputsInput = Renderer::CoreShaderInputsInput;

inline auto AddIssue(
  ValidationReport& report, std::string code, std::string message) -> void
{
  report.issues.push_back(ValidationIssue {
    .code = std::move(code),
    .message = std::move(message),
  });
}

struct SinglePassHarnessStaging {
  std::optional<FrameSessionInput> frame_session;
  std::optional<OutputTargetInput> output_target;
  std::optional<ResolvedViewInput> resolved_view;
  std::optional<PreparedFrameInput> prepared_frame;
  std::optional<CoreShaderInputsInput> core_shader_inputs;
};

class RenderContextMaterializer {
public:
  explicit RenderContextMaterializer(Renderer& renderer)
    : renderer_(oxygen::observer_ptr<Renderer>(&renderer))
  {
  }

  [[nodiscard]] auto ValidateSinglePass(
    const SinglePassHarnessStaging& staging) const -> ValidationReport
  {
    auto report = ValidationReport {};

    if (!staging.frame_session.has_value()) {
      AddIssue(report, "frame_session.missing",
        "Single-pass materialization requires a frame session");
    } else {
      if (staging.frame_session->frame_slot == frame::kInvalidSlot) {
        AddIssue(report, "frame_session.invalid_slot",
          "Single-pass materialization requires a valid frame slot");
      } else if (!(staging.frame_session->frame_slot < frame::kMaxSlot)) {
        AddIssue(report, "frame_session.out_of_bounds_slot",
          "Single-pass materialization requires a frame slot inside the "
          "frames-in-flight range");
      }
      if (!std::isfinite(staging.frame_session->delta_time_seconds)
        || staging.frame_session->delta_time_seconds <= 0.0F) {
        AddIssue(report, "frame_session.invalid_delta_time",
          "Single-pass materialization requires a finite positive delta time");
      }
    }

    if (!staging.output_target.has_value()
      || staging.output_target->framebuffer == nullptr) {
      AddIssue(report, "output_target.missing",
        "Single-pass materialization requires an output target framebuffer");
    }

    const auto resolved_view_id = staging.resolved_view.has_value()
      ? staging.resolved_view->view_id
      : ViewId {};
    const auto shader_input_view_id = staging.core_shader_inputs.has_value()
      ? staging.core_shader_inputs->view_id
      : ViewId {};

    if (staging.resolved_view.has_value() && resolved_view_id == ViewId {}) {
      AddIssue(report, "resolved_view.invalid_id",
        "Resolved view input requires a valid view id");
    }

    if (staging.core_shader_inputs.has_value()
      && shader_input_view_id == ViewId {}) {
      AddIssue(report, "core_shader_inputs.invalid_id",
        "Core shader input override requires a valid view id");
    }

    if (!staging.resolved_view.has_value() && !staging.core_shader_inputs) {
      AddIssue(report, "core_shader_inputs.unsatisfied",
        "Single-pass materialization requires either a resolved view or an "
        "explicit core shader input override");
    }

    if (staging.resolved_view.has_value() && staging.core_shader_inputs
      && resolved_view_id != shader_input_view_id) {
      AddIssue(report, "view_id.mismatch",
        "Resolved view and core shader input override must target the same "
        "view id");
    }

    return report;
  }

  auto MaterializeSinglePass(const SinglePassHarnessStaging& staging)
    -> std::expected<Renderer::ValidatedSinglePassHarnessContext,
      ValidationReport>
  {
    auto report = ValidateSinglePass(staging);
    if (!report.Ok()) {
      return std::unexpected(std::move(report));
    }

    const auto& session = *staging.frame_session;
    const auto view_id = staging.resolved_view.has_value()
      ? staging.resolved_view->view_id
      : staging.core_shader_inputs->view_id;
    const auto resolved_view = staging.resolved_view.has_value()
      ? staging.resolved_view->value
      : MakeSyntheticResolvedView(*staging.output_target->framebuffer);
    const auto prepared_frame = staging.prepared_frame.has_value()
      ? staging.prepared_frame->value
      : PreparedSceneFrame {};
    return Renderer::ValidatedSinglePassHarnessContext(*renderer_, session,
      view_id, staging.output_target->framebuffer, resolved_view,
      prepared_frame,
      staging.core_shader_inputs.has_value()
        ? std::optional<ViewConstants> { staging.core_shader_inputs->value }
        : std::nullopt);
  }

private:
  [[nodiscard]] static auto MakeSyntheticResolvedView(
    const graphics::Framebuffer& framebuffer) -> ResolvedView
  {
    auto width = 1U;
    auto height = 1U;

    const auto& descriptor = framebuffer.GetDescriptor();
    if (!descriptor.color_attachments.empty()
      && descriptor.color_attachments.front().texture != nullptr) {
      const auto& texture_desc
        = descriptor.color_attachments.front().texture->GetDescriptor();
      width = texture_desc.width;
      height = texture_desc.height;
    } else if (descriptor.depth_attachment.texture != nullptr) {
      const auto& texture_desc
        = descriptor.depth_attachment.texture->GetDescriptor();
      width = texture_desc.width;
      height = texture_desc.height;
    }

    auto params = ResolvedView::Params {};
    params.view_config.viewport = {
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    };
    return ResolvedView(params);
  }

  observer_ptr<Renderer> renderer_;
};

} // namespace oxygen::engine::internal
