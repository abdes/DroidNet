//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/api_export.h"
#include "Oxygen/Base/Macros.h"

namespace oxygen::renderer {

  class Disposable
  {
  public:
    Disposable() = default;
    OXYGEN_API virtual ~Disposable();

    OXYGEN_DEFAULT_COPYABLE(Disposable);
    OXYGEN_DEFAULT_MOVABLE(Disposable);

    void Release()
    {
      if (!should_release_) return;
      OnRelease();
      should_release_ = false;
    }

  protected:
    [[nodiscard]] auto ShouldRelease() const -> bool { return should_release_; }
    void ShouldRelease(const bool value) { should_release_ = value; }

    virtual void OnRelease() = 0;

  private:
    bool should_release_{ false };
  };

}  // namespace oxygen::renderer
