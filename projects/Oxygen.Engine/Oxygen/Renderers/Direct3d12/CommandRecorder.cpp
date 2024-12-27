//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/CommandRecorder.h"

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Types.h"
#include "Oxygen/Renderers/Direct3d12/Renderer.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

using namespace oxygen::renderer::d3d12;

CommandRecorder::CommandRecorder(const CommandListType type)
  : Base(type)
{
  LOG_F(1, "{} Command Recorder created", nostd::to_string(type));
}

void CommandRecorder::Begin() {
  DCHECK_F(current_command_list_ == nullptr);

  // TODO: consider recycling command lists
  auto command_list = std::make_unique<CommandList>();
  CHECK_NOTNULL_F(command_list);
  try {
    command_list->Initialize(GetQueueType());
    CHECK_EQ_F(command_list->GetState(), CommandList::State::kFree);
    command_list->OnBeginRecording();
  }
  catch (const std::exception& e) {
    LOG_F(ERROR, "Failed to begin recording to a command list: {}", e.what());
    throw;
  }

  current_command_list_ = std::move(command_list);
}

oxygen::renderer::CommandListPtr CommandRecorder::End() {
  if (!current_command_list_) {
    throw std::runtime_error("No CommandList is being recorded");
  }

  current_command_list_->OnEndRecording();
  return std::move(current_command_list_);
}

void CommandRecorder::Clear(uint32_t flags, uint32_t num_targets, const uint32_t* slots, const glm::vec4* colors,
                            float depth_value, uint8_t stencil_value)
{
  auto& renderer = detail::GetRenderer();
  auto& rtv_heap = renderer.RtvHeap();
  auto& dsv_heap = renderer.DsvHeap();

}

void CommandRecorder::OnRelease()
{
  current_command_list_.reset();
  LOG_F(INFO, "Command Recorder released");
}
