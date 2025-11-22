//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {
class Graphics final : public oxygen::Graphics {
public:
  explicit Graphics(const SerializedBackendConfig& config);

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_MAKE_NON_MOVABLE(Graphics)

  ~Graphics() override = default;

  OXGN_HDLS_NDAPI auto GetDescriptorAllocator() const
    -> const DescriptorAllocator& override;

  OXGN_HDLS_NDAPI auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<Texture> override;

  OXGN_HDLS_NDAPI auto CreateTextureFromNativeObject(const TextureDesc& desc,
    const NativeResource& native) const -> std::shared_ptr<Texture> override;

  OXGN_HDLS_NDAPI auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;

  OXGN_HDLS_NDAPI auto CreateCommandQueue(const QueueKey& queue_key,
    QueueRole role) -> std::shared_ptr<graphics::CommandQueue> override;

  OXGN_HDLS_NDAPI auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    observer_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface> override;

  OXGN_HDLS_NDAPI auto CreateSurfaceFromNative(void* native_handle,
    observer_ptr<graphics::CommandQueue> command_queue) const
    -> std::shared_ptr<Surface> override;

  [[nodiscard]] auto GetShader(std::string_view unique_id) const
    -> std::shared_ptr<IShaderByteCode> override;

protected:
  OXGN_HDLS_NDAPI auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder> override;

  OXGN_HDLS_NDAPI auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;
};

} // namespace oxygen::graphics::headless
