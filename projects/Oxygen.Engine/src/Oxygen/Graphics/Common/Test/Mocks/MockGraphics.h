//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/Surface.h>

namespace oxygen::graphics::testing {

// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockGraphics : public oxygen::Graphics {
public:
  explicit MockGraphics(const std::string_view name)
    : Graphics(name)
  {
  }

  // clang-format off
  // NOLINTBEGIN
  MOCK_METHOD((std::shared_ptr<CommandQueue>), CreateCommandQueue, (const QueueKey&, QueueRole), (override));
  MOCK_METHOD((const DescriptorAllocator&), GetDescriptorAllocator, (), (const, override));
  MOCK_METHOD((std::unique_ptr<Surface>), CreateSurface, (std::weak_ptr<platform::Window>, observer_ptr<CommandQueue>), (const, override));
  MOCK_METHOD((std::shared_ptr<Surface>), CreateSurfaceFromNative, (void*, observer_ptr<CommandQueue>), (const, override));
  MOCK_METHOD((std::shared_ptr<IShaderByteCode>), GetShader, (const ShaderRequest&), (const, override));
  MOCK_METHOD((std::shared_ptr<Texture>), CreateTexture, (const TextureDesc&), (const, override));
  MOCK_METHOD((std::shared_ptr<Texture>), CreateTextureFromNativeObject, (const TextureDesc&, const NativeResource&), (const, override));
  MOCK_METHOD((std::shared_ptr<Buffer>), CreateBuffer, (const BufferDesc&), (const, override));
  MOCK_METHOD((std::unique_ptr<CommandList>), CreateCommandListImpl, (QueueRole, std::string_view), (override));
  MOCK_METHOD((std::unique_ptr<CommandRecorder>), CreateCommandRecorder, (std::shared_ptr<CommandList>, observer_ptr<CommandQueue>), (override));
  MOCK_METHOD((observer_ptr<ReadbackManager>), GetReadbackManager, (), (const, override));
  // NOLINTEND
  // clang-format on
};

} // namespace oxygen::graphics::testing
