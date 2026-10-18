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
        var node = new SceneNode(scene) { Name = "NodeValue" };
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
        var node = new SceneNode(scene) { Name = "NodeValue" };
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
        var node = new SceneNode(scene) { Name = "NodeValue" };
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
        var node = new SceneNode(scene) { Name = "NodeValue" };
        var unattachedCamera = new PerspectiveCamera { Name = "Unattached" };

        var removed = node.RemoveComponent(unattachedCamera);

        _ = removed.Should().BeFalse();
    }

    [TestMethod]
    public void AddComponent_And_RemoveComponent_WhenComponentNull_ThrowArgumentNullException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };

        Action addNull = () => node.AddComponent(null!);
        Action removeNull = () => node.RemoveComponent(null!);

        _ = addNull.Should().Throw<ArgumentNullException>();
        _ = removeNull.Should().Throw<ArgumentNullException>();
    }
}
