//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>

namespace oxygen::scene {

//=== SceneTraversal Implementation ===--------------------------------------//

SceneTraversal::SceneTraversal(const Scene& scene)
    : scene_(&scene)
{
    // Pre-allocate children buffer to avoid repeated small reservations
    children_buffer_.reserve(8);
}

//=== Helper Methods ===-----------------------------------------------------//

auto SceneTraversal::GetNodeImpl(const ResourceHandle& handle) const
    -> SceneNodeImpl*
{
    if (!scene_->Contains(handle)) {
        return nullptr;
    }
    // TODO: Breaks const-correctness but some visitors need mutation.
    // Need better solution for mutable access.
    return &const_cast<Scene*>(scene_)->GetNodeImplRef(handle);
}

void SceneTraversal::InitializeNodes(
    const std::span<const ResourceHandle> handles,
    std::vector<SceneNodeImpl*>& nodes) const
{
    nodes.clear();
    nodes.reserve(handles.size());

    for (const auto& handle : handles) {
        if (auto* node = GetNodeImpl(handle)) [[likely]] {
            nodes.push_back(node);
        }
    }
}

//=== Transform Update Methods ===-------------------------------------------//

auto SceneTraversal::UpdateTransforms() const -> std::size_t
{
    std::size_t updated_count = 0;

    // Batch process with dirty transform filter for efficiency
    auto result = Traverse(
        [&updated_count](SceneNodeImpl& node, const Scene& scene) -> VisitResult {
            node.UpdateTransforms(scene);
            ++updated_count;
            return VisitResult::kContinue;
        },
        TraversalOrder::kDepthFirst,
        DirtyTransformFilter {});

    return updated_count;
}

auto SceneTraversal::UpdateTransformsFrom(
    const std::span<const ResourceHandle> root_handles) const -> std::size_t
{
    std::size_t updated_count = 0;

    // Batch process from specific roots with dirty transform filter
    auto result = TraverseFrom(
        root_handles,
        [&updated_count](SceneNodeImpl& node, const Scene& scene) -> VisitResult {
            node.UpdateTransforms(scene);
            ++updated_count;
            return VisitResult::kContinue;
        },
        TraversalOrder::kDepthFirst,
        DirtyTransformFilter {});

    return updated_count;
}

} // namespace oxygen::scene
