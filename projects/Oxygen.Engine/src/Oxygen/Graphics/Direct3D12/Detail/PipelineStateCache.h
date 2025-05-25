//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/AlwaysFalse.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12 {
class Graphics;

namespace detail {

    /*!
     PipelineStateCache component for D3D12 pipeline state and root signature caching.
     Stores and manages graphics and compute pipeline state objects (PSO) and root signatures,
     keyed by a hash of the pipeline description. Exposes access to the cached pipeline descriptions
     for debugging and inspection.
    */
    class PipelineStateCache final : public Component {
        OXYGEN_COMPONENT(PipelineStateCache)

    public:
        explicit PipelineStateCache(d3d12::Graphics* gfx)
            : gfx_(gfx)
        {
        }
        ~PipelineStateCache() override;

        OXYGEN_MAKE_NON_COPYABLE(PipelineStateCache)
        OXYGEN_DEFAULT_MOVABLE(PipelineStateCache)

        struct Entry {
            dx::IPipelineState* pipeline_state;
            dx::IRootSignature* root_signature;
        };

        /*!
         Get or create a pipeline state object and root signature for the given description and hash.
         \param root_signature The root signature to use for the pipeline state.
         \param desc The pipeline description (graphics or compute).
         \param hash The precomputed hash of the description.
         \return Pair of pointers to the PSO and root signature.
        */
        template <typename TDesc>
        // ReSharper disable once CppNotAllPathsReturnValue
        auto GetOrCreatePipeline(dx::IRootSignature* root_signature, TDesc desc, size_t hash) -> Entry
        {
            if constexpr (std::same_as<TDesc, GraphicsPipelineDesc>) {
                return GetOrCreateGraphicsPipeline(root_signature, std::move(desc), hash);
            } else if constexpr (std::same_as<TDesc, ComputePipelineDesc>) {
                return GetOrCreateComputePipeline(root_signature, std::move(desc), hash);
            } else {
                static_assert(oxygen::always_false_v<TDesc>, "Unsupported pipeline desc type");
            }
        }

        /*!
         Get the cached pipeline description for a given hash.
         \param hash The hash of the pipeline description.
         \return Reference to the cached pipeline description.
         \throws std::out_of_range if not found.
        */
        template <typename TDesc>
        // ReSharper disable once CppNotAllPathsReturnValue
        auto GetPipelineDesc(const size_t hash) const -> const TDesc&
        {
            if constexpr (std::same_as<TDesc, GraphicsPipelineDesc>) {
                return GetGraphicsPipelineDesc(hash);
            } else if constexpr (std::same_as<TDesc, ComputePipelineDesc>) {
                return GetComputePipelineDesc(hash);
            } else {
                static_assert(oxygen::always_false_v<TDesc>, "Unsupported pipeline desc type");
            }
        }

    private:
        // Main pipeline state creation methods
        auto GetOrCreateGraphicsPipeline(dx::IRootSignature* root_signature, GraphicsPipelineDesc desc, size_t hash) -> Entry;
        auto GetOrCreateComputePipeline(dx::IRootSignature* root_signature, ComputePipelineDesc desc, size_t hash) -> Entry;

        // Cache access methods
        auto GetGraphicsPipelineDesc(size_t hash) const -> const GraphicsPipelineDesc&;
        auto GetComputePipelineDesc(size_t hash) const -> const ComputePipelineDesc&;

        // Pipeline cache storage
        std::unordered_map<size_t, std::tuple<GraphicsPipelineDesc, Entry>> graphics_pipelines_;
        std::unordered_map<size_t, std::tuple<ComputePipelineDesc, Entry>> compute_pipelines_;
        d3d12::Graphics* gfx_;
    };

} // namespace detail
} // namespace oxygen::graphics::d3d12
