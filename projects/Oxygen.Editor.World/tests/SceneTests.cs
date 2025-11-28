// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Moq;

namespace Oxygen.Editor.World.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Scene)}.Json")]
public class SceneTests
{
    private readonly Mock<IProject> projectMock = new();

    private IProject ExampleProject => this.projectMock.Object;

    [TestMethod]
    public void Dehydrate_Returns_SceneData_With_RootNodes()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Scene Name" };
        var node1 = new SceneNode(scene) { Name = "Node 1" };
        var node2 = new SceneNode(scene) { Name = "Node 2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var dto = scene.Dehydrate();

        // Assert - DTO shape and values (unit-level check, no JSON round-trip)
        _ = dto.Should().NotBeNull();
        _ = dto.Name.Should().Be("Scene Name");
        _ = dto.RootNodes.Should().HaveCount(2);
        _ = dto.RootNodes[0].Name.Should().Be("Node 1");
        _ = dto.RootNodes[1].Name.Should().Be("Node 2");
    }

    [TestMethod]
    public void Hydrate_Populates_Scene_From_SceneData()
    {
        // Arrange
        // Act: build DTO in code then hydrate into domain Scene (more robust than relying on string literals)
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000000"),
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000001"),
                    Name = "Node 1",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000002"),
                    Name = "Node 2",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert - hydrate logic sets values and attaches created nodes to the scene
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.RootNodes[0].Name.Should().Be("Node 1");
        _ = scene.RootNodes[1].Name.Should().Be("Node 2");
        foreach (var node in scene.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }

    [TestMethod]
    public void Hydrate_With_Empty_RootNodes_Populates_Empty_RootNodes()
    {
        // Arrange
        // Act: construct DTO equivalent to the JSON literal used previously
        var data = new Serialization.SceneData { Id = Guid.Parse("00000000-0000-0000-0000-000000000010"), Name = "Scene Name", RootNodes = [] };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        _ = scene.Name.Should().Be("Scene Name");
        _ = scene.Project.Should().BeSameAs(this.ExampleProject);
        _ = scene.RootNodes.Should().BeEmpty();
    }

    [TestMethod]
    public void CreateAndHydrate_Throws_When_Required_Properties_Missing()
    {
        // Arrange: Scene factory requires Name to be set by caller. Creating a DTO with missing Name
        // should still allow the factory to accept data, but CreateAndHydrate uses the DTO.Name as the scene
        // Name. We assert that creating a Scene with null Name is not allowed via object initializer.
        var data = new Serialization.SceneData { Id = Guid.NewGuid(), Name = null!, RootNodes = [] };

        // Act: factory CreateAndHydrate expects a non-null Name (the code assigns Name first). Simulate misuse.
        var act = () => Scene.CreateAndHydrate(this.ExampleProject, data);

        // Assert - creating a scene with a null name should fail validation
        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Dehydrate_Then_Hydrate_Restores_SceneContents()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "RoundTrip Scene" };
        var node1 = new SceneNode(scene) { Name = "Node A" };
        var node2 = new SceneNode(scene) { Name = "Node B" };

        // avoid adding abstract GameComponent; keep default transform only
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act: Dehydrate -> DTO then hydrate a new Scene using the DTO (unit check; no JSON serializer)
        var dto = scene.Dehydrate();
        var deserialized = new Scene(this.ExampleProject) { Name = dto.Name, Id = dto.Id };
        deserialized.Hydrate(dto);

        // Assert basic structural equivalence
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Name.Should().Be(scene.Name);
        _ = deserialized.Project.Should().BeSameAs(this.ExampleProject);
        _ = deserialized.RootNodes.Should().HaveCount(2);
        _ = deserialized.RootNodes[0].Name.Should().Be("Node A");
        _ = deserialized.RootNodes[1].Name.Should().Be("Node B");

        // Nodes should reference their parent scene
        foreach (var n in deserialized.RootNodes)
        {
            _ = n.Scene.Should().BeSameAs(deserialized);
        }
    }

    [TestMethod]
    public void Hydrate_Node_Sets_NodeScene_For_All_RootNodes()
    {
        // Arrange: construct DTO programmatically (avoid JSON literal fragility)
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000040"),
            Name = "Scene Name",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000041"),
                    Name = "Node 1",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000042"),
                    Name = "Node 2",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        // Assert
        foreach (var node in scene!.RootNodes)
        {
            _ = node.Scene.Should().BeSameAs(scene);
        }
    }

    [TestMethod]
    public void AddChild_Attaches_Child_And_Sets_Parent()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);

        _ = child.Parent.Should().BeSameAs(parent);
        _ = parent.Children.Should().Contain(child);
    }

    [TestMethod]
    public void RemoveChild_Detaches_Child()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent.AddChild(child);
        parent.RemoveChild(child);

        _ = child.Parent.Should().BeNull();
        _ = parent.Children.Should().NotContain(child);
    }

    [TestMethod]
    public void SetParent_Reparents_Node_From_Old_To_New_Parent()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent1 = new SceneNode(scene) { Name = "Parent1" };
        var parent2 = new SceneNode(scene) { Name = "Parent2" };
        var child = new SceneNode(scene) { Name = "Child" };

        parent1.AddChild(child);
        parent2.AddChild(child); // Should move from parent1 to parent2

        _ = child.Parent.Should().BeSameAs(parent2);
        _ = parent1.Children.Should().NotContain(child);
        _ = parent2.Children.Should().Contain(child);
    }

    [TestMethod]
    public void SetParent_Direct_Circular_Reference_Throws()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var node = new SceneNode(scene) { Name = "Node" };

        var act = () => node.AddChild(node);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void SetParent_Indirect_Circular_Reference_Throws()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var parent = new SceneNode(scene) { Name = "Parent" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        parent.AddChild(child);
        child.AddChild(grandChild);

        var act = () => grandChild.AddChild(parent);

        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*circular reference*");
    }

    [TestMethod]
    public void Descendants_Returns_All_Descendants_In_Tree()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child1 = new SceneNode(scene) { Name = "Child1" };
        var child2 = new SceneNode(scene) { Name = "Child2" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child1);
        root.AddChild(child2);
        child1.AddChild(grandChild);

        var descendants = root.Descendants().ToList();

        _ = descendants.Should().HaveCount(3);
        _ = descendants.Should().Contain(child1);
        _ = descendants.Should().Contain(child2);
        _ = descendants.Should().Contain(grandChild);
    }

    [TestMethod]
    public void Ancestors_Returns_Ordered_Ancestors()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };
        var grandChild = new SceneNode(scene) { Name = "GrandChild" };

        root.AddChild(child);
        child.AddChild(grandChild);

        var ancestors = grandChild.Ancestors().ToList();

        _ = ancestors.Should().HaveCount(2);
        _ = ancestors[0].Should().BeSameAs(child);
        _ = ancestors[1].Should().BeSameAs(root);
    }

    [TestMethod]
    public void Dehydrate_Includes_Nested_Children()
    {
        var scene = new Scene(this.ExampleProject) { Name = "Scene" };
        var root = new SceneNode(scene) { Name = "Root" };
        var child = new SceneNode(scene) { Name = "Child" };

        root.AddChild(child);
        scene.RootNodes.Add(root);

        var dto = scene.Dehydrate();

        // Verify DTO hierarchy captures nested nodes
        _ = dto.RootNodes.Should().Contain(r => r.Name == "Root");
        var rootDto = dto.RootNodes.Single(r => string.Equals(r.Name, "Root", StringComparison.Ordinal));
        _ = rootDto.Children.Should().Contain(c => c.Name == "Child");
    }

    [TestMethod]
    public void Hydrate_Populates_Nested_Hierarchy()
    {
        // Arrange: construct nested DTO programmatically
        var data = new Serialization.SceneData
        {
            Id = Guid.Parse("00000000-0000-0000-0000-000000000050"),
            Name = "Scene",
            RootNodes = [
                new()
                {
                    Id = Guid.Parse("00000000-0000-0000-0000-000000000051"),
                    Name = "Root",
                    Components =
                    [
                        new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                    ],
                    Children =
                    [
                        new()
                        {
                            Id = Guid.Parse("00000000-0000-0000-0000-000000000052"),
                            Name = "Child",
                            Components =
                            [
                                new Serialization.TransformComponentData { Name = "Transform", Transform = new() },
                            ],
                        },
                    ],
                },
            ],
        };

        var scene = new Scene(this.ExampleProject) { Name = data.Name, Id = data.Id };
        scene.Hydrate(data);

        _ = scene.Should().NotBeNull();
        _ = scene!.RootNodes.Should().ContainSingle();

        var root = scene.RootNodes[0];
        _ = root.Name.Should().Be("Root");
        _ = root.Children.Should().ContainSingle();

        var child = root.Children[0];
        _ = child.Name.Should().Be("Child");
        _ = child.Parent.Should().BeSameAs(root);
        _ = child.Scene.Should().BeSameAs(scene);
    }

    [TestMethod]
    public void GeometryComponent_Targeted_And_Component_Overrides_Are_Preserved_InMemory()
    {
        // Arrange
        var scene = new Scene(this.ExampleProject) { Name = "Geometry Scene" };
        var node = new SceneNode(scene) { Name = "MeshNode" };
        scene.RootNodes.Add(node);

        var geo = new GeometryComponent { Name = "HeroGeometry", Node = node };
        geo.Geometry.Uri = new("asset://Generated/BasicShapes/Cube");

        // component-level override
        var compMat = new Slots.MaterialsSlot();
        compMat.Material.Uri = new("asset://Generated/Materials/Default");
        geo.OverrideSlots.Add(compMat);

        // targeted override for LOD 0, submesh 1
        var target = new GeometryOverrideTarget { LodIndex = 0, SubmeshIndex = 1 };
        var mat = new Slots.MaterialsSlot();
        mat.Material.Uri = new("asset://Generated/Materials/Gold");
        target.OverrideSlots.Add(mat);
        geo.TargetedOverrides.Add(target);

        node.Components.Add(geo);

        // Act: all operations performed in-memory (unit test) — no JSON serialization
        var dto = scene.Dehydrate();
        var restored = new Scene(this.ExampleProject) { Name = dto.Name, Id = dto.Id };
        restored.Hydrate(dto);

        // Assert we have the same in-memory structure
        _ = restored.RootNodes.Should().ContainSingle();
        var rnode = restored.RootNodes[0];
        var rgeo = rnode.Components.OfType<GeometryComponent>().Single();

        _ = rgeo.Geometry.Uri.Should().Be("asset://Generated/BasicShapes/Cube");
        _ = rgeo.OverrideSlots.OfType<Slots.MaterialsSlot>().Should().ContainSingle();
        _ = rgeo.OverrideSlots.OfType<Slots.MaterialsSlot>().First().Material.Uri.Should().Be("asset://Generated/Materials/Default");

        _ = rgeo.TargetedOverrides.Should().ContainSingle();
        var rt = rgeo.TargetedOverrides[0];
        _ = rt.OverrideSlots.OfType<Slots.MaterialsSlot>().Should().ContainSingle();
        _ = rt.OverrideSlots.OfType<Slots.MaterialsSlot>().First().Material.Uri.Should().Be("asset://Generated/Materials/Gold");
    }
}
