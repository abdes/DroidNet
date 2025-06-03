//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/TransformComponent.h>

using oxygen::scene::SceneNodeData;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::SceneNodeImpl;

using ::testing::NiceMock;
using ::testing::Return;

namespace {

class SceneNodeImplTest : public ::testing::Test {
protected:
    SceneNodeImplTest() = default;
    // Helper to create a node with default flags
    static SceneNodeImpl CreateDefaultNode(const std::string& name = "TestNode")
    {
        return SceneNodeImpl(name);
    }
};

TEST_F(SceneNodeImplTest, DefaultFlagsAreSetCorrectly)
{
    const SceneNodeImpl node("TestNode");
    const auto& data = node.GetComponent<SceneNodeData>();
    const auto& flags = data.GetFlags();
    for (int i = 0; i < static_cast<int>(SceneNodeFlags::kCount); ++i) {
        auto flag = static_cast<SceneNodeFlags>(i);
        switch (flag) {
        case SceneNodeFlags::kVisible:
            EXPECT_TRUE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kStatic:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kCastsShadows:
        case SceneNodeFlags::kReceivesShadows:
        case SceneNodeFlags::kRayCastingSelectable:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_TRUE(flags.IsInherited(flag));
            break;
        case SceneNodeFlags::kIgnoreParentTransform:
            EXPECT_FALSE(flags.GetEffectiveValue(flag));
            EXPECT_FALSE(flags.IsInherited(flag));
            break;
        default:
            break;
        }
        // All other bits should be false by default
        EXPECT_FALSE(flags.GetPendingValue(flag)) << "Pending bit should be false for flag " << i;
        EXPECT_FALSE(flags.IsDirty(flag)) << "Dirty bit should be false for flag " << i;
        EXPECT_FALSE(flags.GetPreviousValue(flag)) << "Previous bit should be false for flag " << i;
    }
}

TEST_F(SceneNodeImplTest, NameIsStoredAndMutable)
{
    SceneNodeImpl node("TestNode");
    EXPECT_EQ(node.GetName(), "TestNode");
    node.SetName("Renamed");
    EXPECT_EQ(node.GetName(), "Renamed");
}

TEST_F(SceneNodeImplTest, TransformDirtyFlagWorks)
{
    SceneNodeImpl node = CreateDefaultNode();
    EXPECT_TRUE(node.IsTransformDirty());
    node.ClearTransformDirty();
    EXPECT_FALSE(node.IsTransformDirty());
    node.MarkTransformDirty();
    EXPECT_TRUE(node.IsTransformDirty());
}

TEST_F(SceneNodeImplTest, HierarchyAccessorsAndMutators)
{
    SceneNodeImpl node = CreateDefaultNode();
    oxygen::ResourceHandle parent { 42 };
    oxygen::ResourceHandle child { 43 };
    oxygen::ResourceHandle next { 44 };
    oxygen::ResourceHandle prev { 45 };

    node.SetParent(parent);
    node.SetFirstChild(child);
    node.SetNextSibling(next);
    node.SetPrevSibling(prev);

    EXPECT_EQ(node.GetParent(), parent);
    EXPECT_EQ(node.GetFirstChild(), child);
    EXPECT_EQ(node.GetNextSibling(), next);
    EXPECT_EQ(node.GetPrevSibling(), prev);
}

// Mock Scene for UpdateTransforms
class MockScene : public oxygen::scene::Scene {
public:
    MockScene()
        : oxygen::scene::Scene("MockScene")
    {
    }
};

TEST_F(SceneNodeImplTest, UpdateTransformsAsRoot)
{
    SceneNodeImpl node = CreateDefaultNode();
    node.SetParent(oxygen::ResourceHandle {}); // Invalid handle = root

    NiceMock<MockScene> mock_scene;
    // No parent, so GetNode should not be called

    node.MarkTransformDirty();
    node.UpdateTransforms(mock_scene);
    EXPECT_FALSE(node.IsTransformDirty());
}

} // namespace
