//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>

#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorHeapSegment.h"

using oxygen::graphics::detail::BaseDescriptorAllocatorConfig;

namespace oxygen::graphics::bindless::testing {

class NoGrowthDescriptorAllocationStrategy : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(ResourceViewType view_type, DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        auto desc = default_strategy.GetHeapDescription(heap_key);
        desc.allow_growth = false;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class ZeroCapacityDescriptorAllocationStrategy : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(ResourceViewType view_type, DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        HeapDescription desc = default_strategy.GetHeapDescription(heap_key);
        desc.allow_growth = true; // growth is allowed but should be ignored
        desc.cpu_visible_capacity = 0;
        desc.shader_visible_capacity = 0;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class OneCapacityDescriptorAllocationStrategy : public DescriptorAllocationStrategy {
public:
    auto GetHeapKey(ResourceViewType view_type, DescriptorVisibility visibility) const
        -> std::string override
    {
        return default_strategy.GetHeapKey(view_type, visibility);
    }

    auto GetHeapDescription(const std::string& heap_key) const
        -> const HeapDescription& override
    {
        // Reuse the default strategy's heap description but disable growth
        HeapDescription desc = default_strategy.GetHeapDescription(heap_key);
        desc.allow_growth = true;
        desc.cpu_visible_capacity = 1;
        desc.shader_visible_capacity = 1;
        // Update the heap description in the cache and return it
        return heap_descriptions_[heap_key] = desc;
    }

private:
    DefaultDescriptorAllocationStrategy default_strategy;
    mutable std::unordered_map<std::string, HeapDescription> heap_descriptions_;
};

class BaseDescriptorAllocatorTest : public ::testing::Test {
protected:
    BaseDescriptorAllocatorConfig default_config {
        .heap_strategy = std::make_shared<DefaultDescriptorAllocationStrategy>(),
    };
    std::unique_ptr<MockDescriptorAllocator> allocator;

    void SetUp() override
    {
        allocator = std::make_unique<MockDescriptorAllocator>(default_config);
    }

    void DisableGrowth()
    {
        allocator = std::make_unique<MockDescriptorAllocator>(BaseDescriptorAllocatorConfig {
            .heap_strategy = std::make_shared<NoGrowthDescriptorAllocationStrategy>(),
        });
    }
};

} // namespace oxygen::graphics::bindless::testing
