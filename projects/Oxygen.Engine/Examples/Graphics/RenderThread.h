//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <functional>

#include <Oxygen/Graphics/Common/Graphics.h>

namespace oxygen {
class RenderThread {
public:
    using RenderTask = std::function<void(oxygen::Graphics&)>;

    RenderThread(std::weak_ptr<oxygen::Graphics> graphics, int frame_lag = 2);

    ~RenderThread();

    void Stop();

    void Submit(RenderTask task);

private:
    void Start();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace oxygen
