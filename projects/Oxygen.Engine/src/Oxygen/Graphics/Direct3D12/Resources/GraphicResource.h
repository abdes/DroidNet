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
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>

namespace D3D12MA {
class Allocation; // NOLINT(*-virtual-class-destructor)
} // namespace D3D12MA

namespace oxygen::graphics {

namespace detail {
    class PerFrameResourceManager;
} // namespace detail

namespace d3d12 {

    class GraphicResource : public Component {
        OXYGEN_COMPONENT(GraphicResource)

    public:
        template <HasReleaseMethod T>
        using ManagedPtrDeleter = std::function<void(T*)>;

        template <HasReleaseMethod T>
        using ManagedPtr = std::unique_ptr<T, ManagedPtrDeleter<T>>;

        template <HasReleaseMethod T>
        static auto WrapForImmediateRelease(T* obj) noexcept
        {
            return ManagedPtr<T>(obj, [](T* wrapped) {
                ObjectRelease(wrapped);
            });
        }

        template <HasReleaseMethod T>
        static auto WrapForDeferredRelease(T* obj,
            graphics::detail::PerFrameResourceManager& resource_manager) noexcept
        {
            return ManagedPtr<T>(obj, [&resource_manager](T* deferred) {
                DeferredObjectRelease(deferred, resource_manager);
            });
        }

        explicit GraphicResource(
            const std::string_view debug_name,
            ManagedPtr<ID3D12Resource> resource,
            ManagedPtr<D3D12MA::Allocation> allocation = nullptr)
            : resource_(std::move(resource))
            , allocation_(std::move(allocation))
        {
            assert(resource_);
            SetName(debug_name);
        }

        ~GraphicResource() noexcept override = default;

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

        [[nodiscard]] auto GetResource() const { return resource_.get(); }

        // ReSharper disable once CppMemberFunctionMayBeConst
        void SetName(const std::string_view name) noexcept
        {
            NameObject(resource_.get(), name);
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

        ManagedPtr<ID3D12Resource> resource_;
        ManagedPtr<D3D12MA::Allocation> allocation_;
    };

    // Non-member swap function for ADL
    inline void swap(GraphicResource& lhs, GraphicResource& rhs) noexcept
    {
        lhs.swap(rhs);
    }

} // namespace d3d12

} // namespace oxygen::graphics
