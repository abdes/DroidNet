//===----------------------------------------------------------------------===//
// Small helper to convert generated root-param table into engine RootBinding
// descriptions. This file consumes the generated `Generated.RootSignature.h`
// and produces consistent RootBindingItem lists.
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Graphics/Common/PipelineState.h>

namespace oxygen::graphics {

// Build root binding items from the generated root-param table.
// The returned vector contains RootBindingItem entries in the same order as
// the generated `kRootParamTable`.
auto BuildRootBindingItemsFromGenerated() -> std::vector<RootBindingItem>;

} // namespace oxygen::graphics
