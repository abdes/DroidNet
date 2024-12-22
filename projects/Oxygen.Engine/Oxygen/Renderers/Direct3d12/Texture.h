//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Renderers/Direct3d12/D3DResource.h"

namespace oxygen::renderer::d3d12 {

  class Texture : public D3DResource
  {
  public:
    Texture();
    ~Texture() override;

  };

}  // namespace oxygen::renderer::d3d12
