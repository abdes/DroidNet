//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::vsm {

class VsmVirtualAddressSpace {
public:
  OXGN_RNDR_API VsmVirtualAddressSpace() = default;

  OXYGEN_MAKE_NON_COPYABLE(VsmVirtualAddressSpace)
  OXYGEN_MAKE_NON_MOVABLE(VsmVirtualAddressSpace)

  OXGN_RNDR_API ~VsmVirtualAddressSpace();

  OXGN_RNDR_API auto BeginFrame(const VsmVirtualAddressSpaceConfig& config,
    std::uint64_t frame_generation) -> void;

  OXGN_RNDR_API auto AllocateSinglePageLocalLight(
    const VsmSinglePageLightDesc& desc) -> VsmVirtualMapLayout;
  OXGN_RNDR_API auto AllocatePagedLocalLight(const VsmLocalLightDesc& desc)
    -> VsmVirtualMapLayout;
  OXGN_RNDR_API auto AllocateDirectionalClipmap(
    const VsmDirectionalClipmapDesc& desc) -> VsmClipmapLayout;

  OXGN_RNDR_NDAPI auto DescribeFrame() const
    -> const VsmVirtualAddressSpaceFrame&;
  OXGN_RNDR_NDAPI auto BuildRemapTable(
    const VsmVirtualAddressSpaceFrame& previous_frame) const
    -> VsmVirtualRemapTable;
  OXGN_RNDR_NDAPI auto ComputeClipmapReuse(
    const VsmClipmapLayout& previous_layout,
    const VsmClipmapLayout& current_layout,
    const VsmClipmapReuseConfig& config) const -> VsmClipmapReuseResult;

private:
  struct VsmFrameBuildState {
    VsmVirtualAddressSpaceFrame frame {};
    VsmVirtualShadowMapId next_virtual_id { 1 };
    std::uint32_t next_page_table_entry { 0 };
  };

  VsmFrameBuildState build_state_ {};
};

} // namespace oxygen::renderer::vsm
