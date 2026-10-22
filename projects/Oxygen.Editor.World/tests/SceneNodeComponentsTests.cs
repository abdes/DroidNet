// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies component attachment, detachment, and node linkage on <see cref="SceneNode"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneNode))]
public sealed class SceneNodeComponentsTests
{
    [TestMethod]
    public void AddComponent_SetsNodePropertyAndReturnsTrue()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");
        var camera = new PerspectiveCamera { Name = "Camera" };

        var added = node.AddComponent(camera);

        _ = added.Should().BeTrue();
        _ = camera.Node.Should().BeSameAs(node);
        _ = node.Components.Should().Contain(camera);
    }

    [TestMethod]
    public void AddComponent_WhenComponentAlreadyOnNode_ReturnsFalse()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");
        var camera = new PerspectiveCamera { Name = "Camera" };
        _ = node.AddComponent(camera);

        var addedAgain = node.AddComponent(camera);

        _ = addedAgain.Should().BeFalse();
        _ = node.Components.Count(c => ReferenceEquals(c, camera)).Should().Be(1);
    }

    [TestMethod]
    public void RemoveComponent_ClearsNodePropertyAndReturnsTrue()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");
        var camera = new PerspectiveCamera { Name = "Camera" };
        _ = node.AddComponent(camera);

        var removed = node.RemoveComponent(camera);

        _ = removed.Should().BeTrue();
        _ = camera.Node.Should().BeNull();
        _ = node.Components.Should().NotContain(camera);
    }

    [TestMethod]
    public void RemoveComponent_WhenComponentNotOnNode_ReturnsFalse()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");
        var unattachedCamera = new PerspectiveCamera { Name = "Unattached" };

        var removed = node.RemoveComponent(unattachedCamera);

        _ = removed.Should().BeFalse();
    }

    [TestMethod]
    public void AddComponent_WhenComponentIsRequiredKind_ThrowsInvalidOperationException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");

        var addMetadata = () => node.AddComponent(new MetadataComponent { Name = "Other" });
        var addHierarchy = () => node.AddComponent(new HierarchyComponent { Name = "Hierarchy" });
        var addTransform = () => node.AddComponent(new TransformComponent { Name = "Transform" });
        var addFlags = () => node.AddComponent(new NodeFlagsComponent { Name = "Rendering" });

        _ = addMetadata.Should().Throw<InvalidOperationException>();
        _ = addHierarchy.Should().Throw<InvalidOperationException>();
        _ = addTransform.Should().Throw<InvalidOperationException>();
        _ = addFlags.Should().Throw<InvalidOperationException>();
        _ = node.Components.Should().HaveCount(4);
    }

    [TestMethod]
    public void RemoveComponent_WhenComponentIsRequired_ThrowsInvalidOperationException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");

        var removeMetadata = () => node.RemoveComponent(node.Metadata);
        var removeHierarchy = () => node.RemoveComponent(node.Hierarchy);
        var removeTransform = () => node.RemoveComponent(node.Transform);
        var removeFlags = () => node.RemoveComponent(node.Flags);

        _ = removeMetadata.Should().Throw<InvalidOperationException>();
        _ = removeHierarchy.Should().Throw<InvalidOperationException>();
        _ = removeTransform.Should().Throw<InvalidOperationException>();
        _ = removeFlags.Should().Throw<InvalidOperationException>();
        _ = node.Components.Should().HaveCount(4);
    }

    [TestMethod]
    public void AddComponent_And_RemoveComponent_WhenComponentNull_ThrowArgumentNullException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");

        Action addNull = () => node.AddComponent(null!);
        Action removeNull = () => node.RemoveComponent(null!);

        _ = addNull.Should().Throw<ArgumentNullException>();
        _ = removeNull.Should().Throw<ArgumentNullException>();
    }
}
