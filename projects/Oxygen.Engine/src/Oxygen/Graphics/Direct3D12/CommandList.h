//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

class CommandQueue;
class CommandRecorder;
class Graphics;

namespace detail {
  class SynchronizedCommandQueue;
} // namespace detail

class CommandList final : public graphics::CommandList {
  using Base = graphics::CommandList;

public:
  CommandList(std::string_view name, QueueRole type, const Graphics* gfx);

  ~CommandList() noexcept override;

  OXYGEN_MAKE_NON_COPYABLE(CommandList)
  OXYGEN_DEFAULT_MOVABLE(CommandList)

  [[nodiscard]] auto GetCommandList() const { return command_list_; }

  auto SetName(std::string_view name) noexcept -> void override;

protected:
  friend class detail::SynchronizedCommandQueue;
  OXGN_D3D12_API auto OnBeginRecording() -> void override;
  OXGN_D3D12_API auto OnEndRecording() -> void override;

private:
  auto ReleaseCommandList() noexcept -> void;

  friend class CommandRecorder;
  friend class CommandQueue;
  friend class ImGuiModule;

  ID3D12GraphicsCommandList* command_list_ {};
  ID3D12CommandAllocator* command_allocator_ {};
};

}
