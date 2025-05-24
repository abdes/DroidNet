//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <functional>
#include <memory>

#include <d3d12.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>

// ReSharper disable once CppInconsistentNaming
namespace D3D12MA {
class Allocation; // NOLINT(*-virtual-class-destructor)
} // namespace D3D12MA

namespace oxygen::graphics {

namespace d3d12 {

    class GraphicResource final : public Component {
        OXYGEN_COMPONENT(GraphicResource)

    public:
        explicit GraphicResource(
            const std::string_view debug_name,
            ID3D12Resource* resource,
            D3D12MA::Allocation* allocation = nullptr)
            : resource_(resource)
            , allocation_(allocation)
        {
            assert(resource_);
            SetName(debug_name);
        }

        ~GraphicResource() noexcept override
        {
            ObjectRelease(resource_);
            ObjectRelease(allocation_);
        }

        OXYGEN_MAKE_NON_COPYABLE(GraphicResource)

        // NOLINTBEGIN(bugprone-use-after-move)
        GraphicResource(GraphicResource&& other) noexcept
            : Component(std::move(other))
            , resource_(std::exchange(other.resource_, nullptr))
            , allocation_(std::exchange(other.allocation_, nullptr))
        {
        }

        auto operator=(GraphicResource&& other) noexcept -> GraphicResource&
        {
            if (this != &other) {
                GraphicResource temp(std::move(other));
                swap(temp);
            }
            return *this;
        }
        // NOLINTEND(bugprone-use-after-move)

        [[nodiscard]] auto GetResource() const { return resource_; }

        // ReSharper disable once CppMemberFunctionMayBeConst
        void SetName(const std::string_view name) noexcept
        {
            NameObject(resource_, name);
        }

    private:
        // Member swap function to support the move-and-swap idiom
        void swap(GraphicResource& other) noexcept
        {
            using std::swap;
            swap(static_cast<Component&>(*this), other);
            swap(resource_, other.resource_);
            swap(allocation_, other.allocation_);
        }

        friend void swap(GraphicResource& lhs, GraphicResource& rhs) noexcept;

        ID3D12Resource* resource_;
        D3D12MA::Allocation* allocation_;
    };

    // Non-member swap function for ADL
    inline void swap(GraphicResource& lhs, GraphicResource& rhs) noexcept
    {
        lhs.swap(rhs);
    }

} // namespace d3d12

} // namespace oxygen::graphics
