//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Resource.h"
#include "Oxygen/Renderers/Common/Types.h"

namespace oxygen::renderer {

  class Surface : public Resource<resources::kSurface>
  {
  public:
    explicit Surface(const resources::SurfaceId& surface_id) : Resource(surface_id) {}
    explicit Surface() = default; // Create a Surface with an invalid id
    ~Surface() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Surface);
    OXYGEN_MAKE_NON_MOVEABLE(Surface);

    virtual void SetSize(int width, int height) = 0;
    virtual void Present() const = 0;

    void Release()
    {
      if (is_released_) return;
      DoRelease();
      is_released_ = true;
    }

    [[nodiscard]] virtual auto Width() const->uint32_t = 0;
    [[nodiscard]] virtual auto Height() const->uint32_t = 0;
    [[nodiscard]] virtual constexpr auto IsClosed() const -> bool { return is_released_; }

  protected:
    virtual void DoRelease() = 0;

  private:
    bool is_released_{ false };
  };

}  // namespace oxygen::renderer
