//===----------------------------------------------------------------------===//
// Implementation for converting generated root-param table into RootBinding
// items used by GraphicsPipelineDesc builders.
//===----------------------------------------------------------------------===//

#include <limits>

#include "RootParamToBindings.h"
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics {

static ResourceViewType RangeTypeToViewType(
  oxygen::engine::binding::RangeType rt)
{
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

auto BuildRootBindingItemsFromGenerated() -> std::vector<RootBindingItem>
{
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

} // namespace oxygen::graphics
