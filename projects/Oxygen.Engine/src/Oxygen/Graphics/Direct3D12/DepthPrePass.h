// filepath: f:\projects\DroidNet\projects\Oxygen.Engine\src\Oxygen\Graphics\DirectD3D12\DepthPrePass.h
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/DepthPrepass.h>
#include <Oxygen/Graphics/Common/PipelineState.h> // Added include
#include <Oxygen/Graphics/Common/RenderPass.h>
#include <Oxygen/Graphics/Common/Shaders.h> // Added for MakeShaderIdentifier & ShaderType
#include <Oxygen/Graphics/Common/Texture.h> // For TextureViewDescription
#include <Oxygen/Graphics/Direct3D12/Renderer.h> // Changed from Graphics.h
#include <Oxygen/Graphics/Direct3D12/api_export.h>
#include <Oxygen/OxCo/Co.h>

#include <functional> // Added for std::hash
#include <optional> // For std::optional

namespace oxygen::graphics::d3d12 {

class CommandRecorder; // Forward declaration for D3D12 CommandRecorder
// class Texture; // Forward declaration - d3d12::Texture is defined in d3d12/Texture.h, included by Renderer.h or indirectly.

/*!
 \brief Direct3D 12 specific implementation of the DepthPrePass.
*/
class DepthPrePass : public graphics::DepthPrePass {
    using Base = graphics::DepthPrePass;

public:
    //! Constructor for D3D12 DepthPrePass.
    /*!
     \param name The name of this render pass.
     \param config The configuration settings for this depth pre-pass.
     \param renderer Pointer to the D3D12 renderer.
    */
    OXYGEN_D3D12_API explicit DepthPrePass(
        std::string_view name,
        const graphics::DepthPrePassConfig& config,
        d3d12::Renderer* renderer);

    //! Destructor.
    OXYGEN_D3D12_API ~DepthPrePass() override = default;

    OXYGEN_DEFAULT_COPYABLE(DepthPrePass)
    OXYGEN_DEFAULT_MOVABLE(DepthPrePass)

    //! Prepares resources for the D3D12 depth pre-pass.
    /*!
     Calls the base class PrepareResources and then performs any D3D12-specific
     resource preparations, such as interpreting clear_color_ for depth/stencil
     clears if applicable, and handling the optional framebuffer.
     It also builds the PSO if not already built.
     \param recorder The D3D12 command recorder.
    */
    OXYGEN_D3D12_API oxygen::co::Co<> PrepareResources(graphics::CommandRecorder& command_recorder) override;

    //! Executes the D3D12 depth pre-pass.
    /*!
     Sets the PSO, DSV, viewport, scissor, and issues draw calls for all
     render items in the draw list.
     \param recorder The D3D12 command recorder.
    */
    OXYGEN_D3D12_API oxygen::co::Co<> Execute(graphics::CommandRecorder& command_recorder) override;

private:
    auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc;
    auto NeedRebuildPipelineState() const -> bool;

    // Helper methods for Execute()
    D3D12_CPU_DESCRIPTOR_HANDLE PrepareAndClearDepthStencilView(d3d12::CommandRecorder& d3d12_recorder, const graphics::Texture& d3d12_depth_texture);
    void SetRenderTargetsAndViewport(d3d12::CommandRecorder& d3d12_recorder, D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle, const graphics::Texture& depth_texture_sptr);
    void IssueDrawCalls(d3d12::CommandRecorder& d3d12_recorder);

    d3d12::Renderer* renderer_ = nullptr;

    dx::IPipelineState* pipeline_state_ { nullptr }; // Pointer to the D3D12 pipeline state object
    dx::IRootSignature* root_signature_ { nullptr }; // Pointer to the D3D12 root signature

    graphics::GraphicsPipelineDesc last_built_pso_desc_;
    size_t last_built_pso_hash_ { 0 };

    // If specific DSV descriptions are needed beyond what the texture provides by default,
    // you might cache them here or handle descriptor allocation more explicitly.
    // For now, assuming GetDSV() on texture or on-the-fly creation is sufficient.
};

} // namespace oxygen::graphics::d3d12
