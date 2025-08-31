//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {
class Graphics final : public oxygen::Graphics {
public:
  explicit Graphics(const SerializedBackendConfig& config);
  ~Graphics() override = default;

  OXGN_HDLS_NDAPI auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<Texture> override;

  OXGN_HDLS_NDAPI auto CreateTextureFromNativeObject(const TextureDesc& desc,
    const NativeObject& native) const -> std::shared_ptr<Texture> override;

  OXGN_HDLS_NDAPI auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;

  OXGN_HDLS_NDAPI auto CreateCommandQueue(const QueueKey& queue_key,
    QueueRole role) -> std::shared_ptr<graphics::CommandQueue> override;

  OXGN_HDLS_NDAPI auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    std::shared_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface> override;

  [[nodiscard]] auto GetShader(std::string_view unique_id) const
    -> std::shared_ptr<IShaderByteCode> override;

  OXGN_HDLS_NDAPI auto AcquireCommandRecorder(
    std::shared_ptr<graphics::CommandQueue> queue,
    std::shared_ptr<graphics::CommandList> command_list,
    bool immediate_submission = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>>;

  // Submit any command lists that were recorded with deferred submission.
  // This will submit them to their associated target queues and call
  // OnSubmitted() on each command list after successful submission.
  OXGN_HDLS_NDAPI auto SubmitDeferredCommandLists() -> void;

  [[nodiscard]] auto CreateRendererImpl(std::string_view name,
    std::weak_ptr<Surface> surface, frame::SlotCount frames_in_flight)
    -> std::unique_ptr<RenderController> override;

protected:
  [[nodiscard]] OXGN_HDLS_NDAPI auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder>;

  OXGN_HDLS_NDAPI auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;
};

} // namespace oxygen::graphics::headless
