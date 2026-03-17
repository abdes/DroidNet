//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>

namespace oxygen::engine::detail {

struct VirtualShadowScheduleResources {
  std::shared_ptr<graphics::Buffer> schedule_buffer;
  ShaderVisibleIndex schedule_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex schedule_uav { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> schedule_lookup_buffer;
  ShaderVisibleIndex schedule_lookup_uav { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> count_buffer;
  ShaderVisibleIndex count_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex count_uav { kInvalidShaderVisibleIndex };

  std::uint32_t entry_capacity { 0U };

  std::shared_ptr<graphics::Buffer> clear_args_buffer;
  ShaderVisibleIndex clear_args_uav { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> draw_args_buffer;
  ShaderVisibleIndex draw_args_uav { kInvalidShaderVisibleIndex };
  std::uint32_t draw_arg_capacity { 0U };

  std::shared_ptr<graphics::Buffer> draw_page_ranges_buffer;
  ShaderVisibleIndex draw_page_ranges_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_page_ranges_uav { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> draw_page_indices_buffer;
  ShaderVisibleIndex draw_page_indices_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex draw_page_indices_uav { kInvalidShaderVisibleIndex };

  std::shared_ptr<graphics::Buffer> draw_page_counter_buffer;
  ShaderVisibleIndex draw_page_counter_uav { kInvalidShaderVisibleIndex };
  std::uint32_t draw_page_index_capacity { 0U };
};

} // namespace oxygen::engine::detail
