//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/Types/RenderTask.h>

namespace oxygen::graphics {

class RenderTarget;

namespace detail {
  class RenderThread final : public Component {
    OXYGEN_COMPONENT(RenderThread)
    OXYGEN_COMPONENT_REQUIRES(oxygen::ObjectMetaData)
  public:
    using BeginFrameFn = std::function<void()>;
    using EndFrameFn = std::function<void()>;

    explicit RenderThread(uint32_t frames_in_flight,
      BeginFrameFn begin_frame = nullptr, EndFrameFn end_frame = nullptr);

    ~RenderThread() override;

    OXYGEN_MAKE_NON_COPYABLE(RenderThread)
    OXYGEN_DEFAULT_MOVABLE(RenderThread)

    void Submit(FrameRenderTask task);

    void Stop();

  protected:
    void UpdateDependencies(
      const std::function<Component&(TypeId)>& get_component) override;

  private:
    void Start();

    struct Impl;
    std::unique_ptr<Impl> impl_;
  };
} // namespace detail

} // namespace oxygen::graphics
