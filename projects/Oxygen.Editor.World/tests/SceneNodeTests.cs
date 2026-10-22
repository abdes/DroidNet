// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;
using SceneData = Oxygen.Editor.World.Serialization.SceneData;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies <see cref="SceneNode"/> creation, required components, name validation, and hierarchy path queries.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneNode))]
public sealed class SceneNodeTests
{
    [TestMethod]
    public void Constructor_HoldsExactlyTheRequiredComponentsInOrder()
    {
        var scene = new Scene { Name = "Scene" };

        var node = new SceneNode(scene, "Node");

        _ = node.Components.Select(static component => component.GetType()).Should().Equal(
            typeof(MetadataComponent),
            typeof(HierarchyComponent),
            typeof(TransformComponent),
            typeof(NodeFlagsComponent));
        _ = node.Components[0].Should().BeSameAs(node.Metadata);
        _ = node.Components[1].Should().BeSameAs(node.Hierarchy);
        _ = node.Components[2].Should().BeSameAs(node.Transform);
        _ = node.Components[3].Should().BeSameAs(node.Flags);
        _ = node.Components.Should().OnlyContain(component => component.Node == node && component.IsLocked);
        _ = node.Metadata.Name.Should().Be("Node");
    }

    [TestMethod]
    [DataRow("")]
    [DataRow("  ")]
    public void Constructor_WhenNameIsEmptyOrWhitespace_ThrowsArgumentException(string name)
    {
        var scene = new Scene { Name = "Scene" };

        var act = () => new SceneNode(scene, name);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    [DataRow(typeof(MetadataData))]
    [DataRow(typeof(HierarchyData))]
    [DataRow(typeof(TransformData))]
    [DataRow(typeof(NodeFlagsData))]
    public void CreateAndHydrate_WhenARequiredComponentIsMissing_ThrowsJsonException(Type missing)
    {
        var scene = new Scene { Name = "Scene" };
        var components = SceneTestSupport.RequiredComponents("Node");
        _ = components.RemoveAll(component => component.GetType() == missing);
        var data = new SceneNodeData { Id = NodeId.New(), Components = components };

        var act = () => SceneNode.CreateAndHydrate(scene, data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    [DataRow(typeof(MetadataData))]
    [DataRow(typeof(HierarchyData))]
    [DataRow(typeof(TransformData))]
    [DataRow(typeof(NodeFlagsData))]
    public void CreateAndHydrate_WhenARequiredComponentIsRepeated_ThrowsJsonException(Type repeated)
    {
        var scene = new Scene { Name = "Scene" };
        var components = SceneTestSupport.RequiredComponents("Node");
        components.Add(components.Single(component => component.GetType() == repeated) with { Id = ComponentId.New() });
        var data = new SceneNodeData { Id = NodeId.New(), Components = components };

        var act = () => SceneNode.CreateAndHydrate(scene, data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void Hydrate_WhenRequiredComponentsAreMissing_ThrowsJsonException()
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");
        var data = new SceneNodeData { Id = node.Id, Components = [] };

        Action act = () => node.Hydrate(data);

        _ = act.Should().Throw<JsonException>();
    }

    [TestMethod]
    public void Dehydrate_WritesTheHierarchyLastAndNestsChildrenInIt()
    {
        var scene = new Scene { Name = "Scene" };
        var parent = new SceneNode(scene, "Parent");
        var child = new SceneNode(scene, "Child");
        _ = parent.AddComponent(new PerspectiveCamera { Name = "Camera" });
        parent.Hierarchy.AddChild(child);

        var parentData = parent.Dehydrate();
        var childData = child.Dehydrate();

        var hierarchy = parentData.Components[^1].Should().BeOfType<HierarchyData>().Subject;
        _ = parentData.Components.OfType<HierarchyData>().Should().ContainSingle();
        _ = hierarchy.Children.Should().ContainSingle().Which.Id.Should().Be(child.Id);
        _ = childData.Components[^1].Should().BeOfType<HierarchyData>().Which.Children.Should().BeNull();
    }
    [TestMethod]
    public void HierarchyPath_OnRootAndNestedNodes_ReturnsPathSeparatedBySlash()
    {
        var scene = new Scene { Name = "Scene" };
        var level = new SceneNode(scene, "Level");
        var doors = new SceneNode(scene, "Doors");
        var front = new SceneNode(scene, "Front");
        level.Hierarchy.AddChild(doors);
        doors.Hierarchy.AddChild(front);
        scene.RootNodes.Add(level);

        _ = level.HierarchyPath().Should().Be("Level");
        _ = doors.HierarchyPath().Should().Be("Level/Doors");
        _ = front.HierarchyPath().Should().Be("Level/Doors/Front");
    }

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("   ")]
    public void MetadataName_WhenNullEmptyOrWhitespace_ThrowsArgumentException(string? invalidName)
    {
        var scene = new Scene { Name = "Scene" };
        var node = new SceneNode(scene, "Node");

        Action act = () => node.Metadata.Name = invalidName!;

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Dehydrate_Includes_Nested_Children()
    {
        var scene = new Scene { Name = "Scene" };
        var root = new SceneNode(scene, "Root");
        var child = new SceneNode(scene, "Child");

        root.Hierarchy.AddChild(child);
        scene.RootNodes.Add(root);

        var dto = scene.Dehydrate();

        // Verify DTO hierarchy captures nested nodes
        _ = dto.RootNodes.Should().Contain(r => r.Name == "Root");
        var rootDto = dto.RootNodes.Single(r => r.Name == "Root");
        _ = rootDto.Children.Should().Contain(c => c.Name == "Child");
    }

    [TestMethod]
    public void Hydrate_Populates_Nested_Hierarchy()
    {
        // Arrange: construct nested DTO programmatically
        var childData = SceneTestSupport.NodeData("Child") with { Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000052")) };
        var rootData = new SceneNodeData
        {
            Id = NodeId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000051")),
            Components = SceneTestSupport.RequiredComponents("Root", [childData]),
        };
        var data = new SceneData
        {
            Id = SceneId.FromValue(Guid.Parse("00000000-0000-0000-0000-000000000050")),
            Name = "Scene",
            RootNodes = [rootData],
        };

        var scene = new Scene { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        _ = scene.RootNodes.Should().ContainSingle();

        var root = scene.RootNodes[0];
        _ = root.Metadata.Name.Should().Be("Root");
        _ = root.Hierarchy.Children.Should().ContainSingle();

        var child = root.Hierarchy.Children[0];
        _ = child.Metadata.Name.Should().Be("Child");
        _ = child.Hierarchy.Parent.Should().BeSameAs(root);
        _ = child.Scene.Should().BeSameAs(scene);
    }
}
