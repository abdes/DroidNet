//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

//! \file
//! \brief Forward declarations of types and associated smart pointers.

#include <memory>
#include <vector>

namespace oxygen {

class Graphics;
using GraphicsPtr = std::weak_ptr<Graphics>;

namespace graphics {

    struct RendererProperties {
        // TODO: add configuration properties for the renderer
    };
    class Renderer;

    class Buffer;
    class CommandList;
    class CommandQueue;
    class CommandRecorder;
    class IShaderByteCode;
    class PerFrameResourceManager;
    class RenderTarget;
    class ShaderCompiler;
    class Surface;
    class SynchronizationCounter;
    class WindowSurface;

    using BufferPtr = std::shared_ptr<Buffer>;
    using CommandListPtr = std::unique_ptr<CommandList>;
    using CommandRecorderPtr = std::shared_ptr<CommandRecorder>;
    using IShaderByteCodePtr = std::shared_ptr<IShaderByteCode>;
    using RendererPtr = std::weak_ptr<Renderer>;
    using RenderTargetNoDeletePtr = const RenderTarget*;
    using ShaderCompilerPtr = std::shared_ptr<ShaderCompiler>;
    using WindowSurfacePtr = std::unique_ptr<WindowSurface>;

    using CommandLists = std::vector<CommandListPtr>;

    struct MemoryBlockDesc;
    class IMemoryBlock;
    using MemoryBlockPtr = std::shared_ptr<IMemoryBlock>;

} // namespace graphics
} // namespace oxygen
