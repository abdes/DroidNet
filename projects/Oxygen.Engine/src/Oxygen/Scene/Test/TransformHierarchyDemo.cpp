//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/TransformComponent.h>
#include <iostream>
#include <format>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::TransformComponent;

namespace {

void PrintTransform(const SceneNode& node, const std::string& label) {
    if (!node.IsValid()) return;

    auto transform = node.GetComponent<TransformComponent>();
    if (!transform.IsValid()) {
        std::cout << std::format("{}: No transform component\n", label);
        return;
    }

    auto localPos = transform.GetObject().GetLocalPosition();
    auto worldPos = transform.GetObject().GetWorldPosition();

    std::cout << std::format("{}: Local({:.1f}, {:.1f}, {:.1f}) -> World({:.1f}, {:.1f}, {:.1f})\n",
        label,
        localPos.x, localPos.y, localPos.z,
        worldPos.x, worldPos.y, worldPos.z);
}

void DemonstrateTransformInheritance() {
    std::cout << "=== Transform Inheritance Demonstration ===\n\n";

    // Create a scene
    auto scene = std::make_shared<Scene>();

    // Create hierarchy: Root -> Parent -> Child
    auto root = scene->CreateNode("Root");
    auto parent = scene->CreateChildNode(root, "Parent");
    auto child = scene->CreateChildNode(parent, "Child");

    // Add transform components
    auto rootTransform = root.AddComponent<TransformComponent>();
    auto parentTransform = parent.AddComponent<TransformComponent>();
    auto childTransform = child.AddComponent<TransformComponent>();

    std::cout << "1. Initial transforms (all at origin):\n";
    PrintTransform(root, "Root");
    PrintTransform(parent, "Parent");
    PrintTransform(child, "Child");

    // Move root to (10, 0, 0)
    rootTransform.GetObject().SetLocalPosition({10.0f, 0.0f, 0.0f});
    scene->UpdateNodeHierarchy();

    std::cout << "\n2. After moving Root to (10, 0, 0):\n";
    PrintTransform(root, "Root");
    PrintTransform(parent, "Parent");
    PrintTransform(child, "Child");

    // Move parent relative to root: (5, 5, 0)
    parentTransform.GetObject().SetLocalPosition({5.0f, 5.0f, 0.0f});
    scene->UpdateNodeHierarchy();

    std::cout << "\n3. After moving Parent to local (5, 5, 0):\n";
    PrintTransform(root, "Root");
    PrintTransform(parent, "Parent");
    PrintTransform(child, "Child");

    // Move child relative to parent: (0, 0, 3)
    childTransform.GetObject().SetLocalPosition({0.0f, 0.0f, 3.0f});
    scene->UpdateNodeHierarchy();

    std::cout << "\n4. After moving Child to local (0, 0, 3):\n";
    PrintTransform(root, "Root");
    PrintTransform(parent, "Parent");
    PrintTransform(child, "Child");

    std::cout << "\n5. Transform inheritance verification:\n";
    std::cout << "   - Root world position should equal its local position\n";
    std::cout << "   - Parent world position = Root world + Parent local\n";
    std::cout << "   - Child world position = Parent world + Child local\n";

    // Test dirty flag propagation by moving root again
    std::cout << "\n6. Testing dirty flag propagation - moving Root to (0, 10, 0):\n";
    rootTransform.GetObject().SetLocalPosition({0.0f, 10.0f, 0.0f});
    scene->UpdateNodeHierarchy();

    PrintTransform(root, "Root");
    PrintTransform(parent, "Parent");
    PrintTransform(child, "Child");

    std::cout << "\nNote: All children automatically inherited the Root's movement\n";
    std::cout << "because their world transforms were updated during hierarchy update.\n";

    std::cout << "\n=== Transform Inheritance Demonstration Complete ===\n";
}

} // namespace

int main() {
    try {
        DemonstrateTransformInheritance();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
