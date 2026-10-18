// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using SceneData = Oxygen.Editor.World.Serialization.SceneData;
using TransformData = Oxygen.Editor.World.Serialization.TransformData;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies <see cref="SceneNode"/> creation, transform component invariants, name validation, and hierarchy path queries.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneNode))]
public sealed class SceneNodeTests
{
    [TestMethod]
    public void Constructor_InitializesNodeWithSingleLockedTransformComponent()
    {
        var scene = new Scene { Name = "Scene" };

        var node = new SceneNode(scene) { Name = "NodeValue" };

        _ = node.Components.Should().ContainSingle();
        var transform = node.Components[0] as TransformComponent;
        _ = transform.Should().NotBeNull();
        _ = transform!.IsLocked.Should().BeTrue();
        _ = transform.Node.Should().BeSameAs(node);
    }

    [TestMethod]
    public void Hydrate_WhenMissingTransformComponent_ThrowsJsonException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };
        var data = new SceneNodeData
        {
            Id = node.Id,
            Name = node.Name,
            Components = [],
        };

        Action act = () => node.Hydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void Hydrate_WhenMultipleTransformComponents_ThrowsJsonException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };
        var data = new SceneNodeData
        {
            Id = node.Id,
            Name = node.Name,
            Components =
            [
                new TransformData { Name = "Transform 1" },
                new TransformData { Name = "Transform 2" },
            ],
        };

        Action act = () => node.Hydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void HierarchyPath_OnRootAndNestedNodes_ReturnsPathSeparatedBySlash()
    {
        var scene = new Scene { Name = "Scene" };
        var level = new SceneNode(scene) { Name = "Level" };
        var doors = new SceneNode(scene) { Name = "Doors" };
        var front = new SceneNode(scene) { Name = "Front" };
        level.AddChild(doors);
        doors.AddChild(front);
        scene.RootNodes.Add(level);

        _ = level.HierarchyPath().Should().Be("Level");
        _ = doors.HierarchyPath().Should().Be("Level/Doors");
        _ = front.HierarchyPath().Should().Be("Level/Doors/Front");
    }

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("   ")]
    public void Name_WhenNullEmptyOrWhitespace_ThrowsArgumentException(string? invalidName)
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "NodeValue" };

        Action act = () => node.Name = invalidName!;

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Dehydrate_Includes_Nested_Children()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };

        root.AddChild(child);
        scene.RootNodes.Add(root);

        var dto = scene.Dehydrate();

        // Verify DTO hierarchy captures nested nodes
        _ = dto.RootNodes.Should().Contain(r => string.Equals(r.Name, "Root", StringComparison.Ordinal));
        var rootDto = dto.RootNodes.Single(r => string.Equals(r.Name, "Root", StringComparison.Ordinal));
        _ = rootDto.Children.Should().Contain(c => string.Equals(c.Name, "Child", StringComparison.Ordinal));
    }

    [TestMethod]
    public void Hydrate_Populates_Nested_Hierarchy()
    {
        // Arrange: construct nested DTO programmatically
        var data = new SceneData
        {
            Id = SceneId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000050")),
            Name = "Scene",
            RootNodes = [
                new()
                {
                    Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000051")),
                    Name = "Root",
                    Components =
                    [
                        new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                    ],
                    Children =
                    [
                        new()
                        {
                            Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000052")),
                            Name = "Child",
                            Components =
                            [
                                new TransformData { Name = "Transform", Position = default, Rotation = default, Scale = default },
                            ],
                        },
                    ],
                },
            ],
        };

        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        _ = scene.Should().NotBeNull();
        _ = scene.RootNodes.Should().ContainSingle();

        var root = scene.RootNodes[0];
        _ = root.Name.Should().Be("Root");
        _ = root.Children.Should().ContainSingle();

        var child = root.Children[0];
        _ = child.Name.Should().Be("Child");
        _ = child.Parent.Should().BeSameAs(root);
        _ = child.Scene.Should().BeSameAs(scene);
    }
}
