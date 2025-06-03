//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::Scene;
using oxygen::scene::SceneNode;

namespace {

class SceneGraphFunctionalTest : public ::testing::Test {
protected:
    std::shared_ptr<Scene> scene;

    void SetUp() override
    {
        scene = std::make_shared<Scene>("FunctionalTestScene");
    }
};

NOLINT_TEST_F(SceneGraphFunctionalTest, ParentSingleChildRelationship)
{
    // Create a parent node and a single child node.
    auto parent_node = scene->CreateNode("A");
    auto child_node_opt = scene->CreateChildNode(parent_node, "B");
    ASSERT_TRUE(child_node_opt.has_value());
    auto& child_node = *child_node_opt;

    // Validate parent relationship for the child node.
    auto parent_of_child = child_node.GetParent();
    if (parent_of_child.has_value()) {
        auto parent_of_child_impl_opt = parent_of_child.value().GetObject();
        if (parent_of_child_impl_opt.has_value()) {
            const auto& parent_of_child_impl = parent_of_child_impl_opt->get();
            EXPECT_EQ(parent_of_child_impl.GetName(), "A");
        }
    }

    // Validate root and children status for the parent node.
    EXPECT_TRUE(parent_node.IsRoot());
    EXPECT_TRUE(parent_node.HasChildren());

    // Validate that the child node is not a root.
    EXPECT_FALSE(child_node.IsRoot());

    // Validate that the parent node has exactly one child and it is the child node.
    auto first_child = parent_node.GetFirstChild();
    ASSERT_TRUE(first_child.has_value());
    EXPECT_EQ(first_child->GetObject()->get().GetName(), "B");
    auto next_sibling = first_child->GetNextSibling();
    EXPECT_FALSE(next_sibling.has_value());
}

NOLINT_TEST_F(SceneGraphFunctionalTest, SiblingLinksAreCorrect)
{
    auto nodeA = scene->CreateNode("A");
    auto nodeB = scene->CreateChildNode(nodeA, "B");
    auto nodeC = scene->CreateChildNode(nodeA, "C");

    auto firstChild = nodeA.GetFirstChild();
    ASSERT_TRUE(firstChild.has_value());
    auto nextSibling = firstChild->GetNextSibling();
    ASSERT_TRUE(nextSibling.has_value());
    EXPECT_NE(firstChild->GetObject()->get().GetName(), nextSibling->GetObject()->get().GetName());
}

NOLINT_TEST_F(SceneGraphFunctionalTest, NodeInvalidation)
{
    auto nodeA = scene->CreateNode("A");
    EXPECT_TRUE(nodeA.IsValid());
    scene->DestroyNode(nodeA);
    EXPECT_FALSE(nodeA.IsValid());
}

NOLINT_TEST_F(SceneGraphFunctionalTest, CreateAndRetrieveNode)
{
    // Create a node and validate its initial state.
    auto node = scene->CreateNode("TestNode");
    ASSERT_TRUE(node.IsValid());

    // Retrieve the node's object and check its name.
    auto obj_opt = node.GetObject();
    ASSERT_TRUE(obj_opt.has_value());
    const auto& obj = obj_opt->get();
    EXPECT_EQ(obj.GetName(), "TestNode");

    // Node should be valid and root.
    EXPECT_TRUE(node.IsValid());
    EXPECT_TRUE(node.IsRoot());
    auto parent_opt = node.GetParent();
    EXPECT_FALSE(parent_opt.has_value());

    // Node should have no children.
    EXPECT_FALSE(node.HasChildren());
    auto first_child_opt = node.GetFirstChild();
    EXPECT_FALSE(first_child_opt.has_value());

    // Node should have default flags set (visible = true).
    const auto& flags = obj.GetFlags();
    auto visible = flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible);
    EXPECT_TRUE(visible);

    // Node should not have any siblings (no next sibling).
    auto next_sibling_opt = node.GetNextSibling();
    EXPECT_FALSE(next_sibling_opt.has_value());

    // Node's object should not be null.
    EXPECT_TRUE(node.GetObject().has_value());
}

NOLINT_TEST_F(SceneGraphFunctionalTest, CreateModifyRetrieveNodeCycle)
{
    // Create a node.
    auto node = scene->CreateNode("CycleNode");
    ASSERT_TRUE(node.IsValid());

    // Retrieve and check initial name.
    auto obj_opt = node.GetObject();
    ASSERT_TRUE(obj_opt.has_value());
    auto& obj = obj_opt->get();
    EXPECT_EQ(obj.GetName(), "CycleNode");

    // Modify node name and a flag.
    obj.SetName("ModifiedNode");
    auto& flags = obj.GetFlags();
    flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible, false);
    flags.ProcessDirtyFlags();
    EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));

    // Retrieve node again by handle and check modifications.
    auto handle = node.GetHandle();
    auto node_again_opt = scene->GetNode(handle);
    ASSERT_TRUE(node_again_opt.has_value());
    auto& node_again = *node_again_opt;
    ASSERT_TRUE(node_again.IsValid());
    auto obj_opt2 = node_again.GetObject();
    ASSERT_TRUE(obj_opt2.has_value());
    const auto& obj2 = obj_opt2->get();
    EXPECT_EQ(obj2.GetName(), "ModifiedNode");
    const auto& flags2 = obj2.GetFlags();
    EXPECT_FALSE(flags2.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));
}

NOLINT_TEST_F(SceneGraphFunctionalTest, UpdatePropagatesTransformsAndFlags)
{
    // Create a root and a hierarchy: root -> child -> grandchild
    auto root = scene->CreateNode("Root");
    auto child_opt = scene->CreateChildNode(root, "Child");
    ASSERT_TRUE(child_opt.has_value());
    auto& child = *child_opt;
    auto grandchild_opt = scene->CreateChildNode(child, "Grandchild");
    ASSERT_TRUE(grandchild_opt.has_value());
    auto& grandchild = *grandchild_opt;
    auto leaf_opt = scene->CreateChildNode(grandchild, "Leaf");
    ASSERT_TRUE(leaf_opt.has_value());
    auto& leaf = *leaf_opt;

    // Helper to get flags and mark dirty
    auto set_flags = [](SceneNode& node, bool visible, bool ignore_parent, bool mark_dirty_flag, bool mark_transform_dirty) {
        auto obj_opt = node.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        auto& flags = obj.GetFlags();
        if (mark_dirty_flag) {
            // Toggle to opposite value to ensure dirty, then set intended value
            flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible, !visible);
            flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible, visible);
        } else {
            flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kVisible, visible);
        }
        flags.SetLocalValue(oxygen::scene::SceneNodeFlags::kIgnoreParentTransform, ignore_parent);
        if (mark_transform_dirty) {
            obj.MarkTransformDirty();
        }
    };

    // Mix of dirty and clean flags/transforms:
    set_flags(root, true, false, false, true); // dirty transform only
    set_flags(child, false, false, true, false); // dirty flag only
    set_flags(grandchild, true, true, true, true); // both dirty
    set_flags(leaf, false, false, false, false); // clean

    // Call update (should propagate flags and transforms)
    scene->Update();

    // After update, all transforms should be clean
    for (auto node : { root, child, grandchild, leaf }) {
        auto obj_opt = node.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        EXPECT_FALSE(obj.IsTransformDirty());
    }

    // Check effective flags
    {
        auto obj_opt = grandchild.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        const auto& flags = obj.GetFlags();
        EXPECT_TRUE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kIgnoreParentTransform));
        EXPECT_TRUE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));
    }
    {
        auto obj_opt = child.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        const auto& flags = obj.GetFlags();
        EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));
        EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kIgnoreParentTransform));
    }
    {
        auto obj_opt = root.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        const auto& flags = obj.GetFlags();
        EXPECT_TRUE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));
        EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kIgnoreParentTransform));
    }
    {
        auto obj_opt = leaf.GetObject();
        ASSERT_TRUE(obj_opt.has_value());
        auto& obj = obj_opt->get();
        const auto& flags = obj.GetFlags();
        EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kVisible));
        EXPECT_FALSE(flags.GetEffectiveValue(oxygen::scene::SceneNodeFlags::kIgnoreParentTransform));
    }
}

} // namespace
