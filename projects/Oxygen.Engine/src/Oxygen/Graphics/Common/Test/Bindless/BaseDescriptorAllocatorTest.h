//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>

#include <gtest/gtest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>

#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorHeapSegment.h"

namespace oxygen::graphics::bindless::testing {

class NoGrowthDescriptorAllocationStrategy final : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(const ResourceViewType view_type, const DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy_.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        auto desc = default_strategy_.GetHeapDescription(heap_key);
        desc.allow_growth = false;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

    auto GetHeapBaseIndex(ResourceViewType /*view_type*/, DescriptorVisibility /*visibility*/) const
        -> DescriptorHandle::IndexT override
    {
        return 0; // Always return 0 for base index
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy_;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class ZeroCapacityDescriptorAllocationStrategy final : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(const ResourceViewType view_type, const DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy_.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        HeapDescription desc = default_strategy_.GetHeapDescription(heap_key);
        desc.allow_growth = true; // growth is allowed but should be ignored
        desc.cpu_visible_capacity = 0;
        desc.shader_visible_capacity = 0;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

    auto GetHeapBaseIndex(ResourceViewType /*view_type*/, DescriptorVisibility /*visibility*/) const
        -> DescriptorHandle::IndexT override
    {
        return 0; // Always return 0 for base index
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy_;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class OneCapacityDescriptorAllocationStrategy final : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(const ResourceViewType view_type, const DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy_.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        HeapDescription desc = default_strategy_.GetHeapDescription(heap_key);
        desc.allow_growth = true;
        desc.cpu_visible_capacity = 1;
        desc.shader_visible_capacity = 1;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

    auto GetHeapBaseIndex(ResourceViewType view_type, DescriptorVisibility visibility) const
        -> DescriptorHandle::IndexT override
    {
        return default_strategy_.GetHeapBaseIndex(view_type, visibility);
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy_;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class BaseDescriptorAllocatorTest : public ::testing::Test {
protected:
    std::shared_ptr<const DescriptorAllocationStrategy> heap_strategy_;
    std::unique_ptr<::testing::NiceMock<MockDescriptorAllocator>> allocator_;

    void SetUp() override
    {
        heap_strategy_ = std::make_shared<DefaultDescriptorAllocationStrategy>();
        allocator_ = std::make_unique<::testing::NiceMock<MockDescriptorAllocator>>(heap_strategy_);
    }

    void TearDown() override
    {
        // Destroy the allocator before the fixture is destroyed to avoid use
        // after free inside mocked methods in the destructor.
        allocator_.reset();
    }

    void DisableGrowth()
    {
        heap_strategy_ = std::make_shared<NoGrowthDescriptorAllocationStrategy>();
        allocator_ = std::make_unique<::testing::NiceMock<MockDescriptorAllocator>>(heap_strategy_);
    }
};

} // namespace oxygen::graphics::bindless::testing
