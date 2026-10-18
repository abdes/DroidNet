// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;
using SceneData = Oxygen.Editor.World.Serialization.SceneData;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// Verifies core <see cref="Scene"/> invariants, property notifications, and node queries.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Scene))]
public sealed class SceneTests
{
    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    [DataRow("   ")]
    public void Name_WhenNullEmptyOrWhitespace_ThrowsArgumentException(string? invalidName)
    {
        var scene = new Scene { Name = "Valid Initial" };

        Action act = () => scene.Name = invalidName!;

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Name_WhenValid_SetsPropertyAndRaisesNotification()
    {
        var scene = new Scene { Name = "Initial" };
        var recorder = new PropertyChangeRecorder(scene);

        scene.Name = "Valid Scene Name";

        _ = scene.Name.Should().Be("Valid Scene Name");
        _ = recorder.ChangingCount(nameof(Scene.Name)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(Scene.Name)).Should().Be(1);
    }

    [TestMethod]
    public void AllNodes_ReturnsDepthFirstInHierarchyOrderAcrossSeveralRoots()
    {
        var scene = new Scene { Name = "Multi-root Scene" };
        var root1 = new SceneNode(scene) { Name = "Root1" };
        var child1a = new SceneNode(scene) { Name = "Child1A" };
        var child1b = new SceneNode(scene) { Name = "Child1B" };
        var grandChild1 = new SceneNode(scene) { Name = "GrandChild1" };
        root1.AddChild(child1a);
        child1a.AddChild(grandChild1);
        root1.AddChild(child1b);

        var root2 = new SceneNode(scene) { Name = "Root2" };
        var child2a = new SceneNode(scene) { Name = "Child2A" };
        root2.AddChild(child2a);

        scene.RootNodes.Add(root1);
        scene.RootNodes.Add(root2);

        var nodes = scene.AllNodes.ToList();

        _ = nodes.Should().Equal([root1, child1a, grandChild1, child1b, root2, child2a]);
    }

    [TestMethod]
    public void SetEnvironment_WhenValueDiffers_RaisesPropertyChangingAndPropertyChangedExactlyOnce()
    {
        var scene = new Scene { Name = "Scene" };
        var recorder = new PropertyChangeRecorder(scene);
        var newEnv = new SceneEnvironmentData
        {
            AtmosphereEnabled = false,
            Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.5f, 0.5f, 0.5f) },
        };

        scene.SetEnvironment(newEnv);

        _ = recorder.ChangingCount(nameof(Scene.Environment)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(Scene.Environment)).Should().Be(1);
    }

    [TestMethod]
    public void SetEnvironment_WhenValueIsEqual_RaisesNoNotifications()
    {
        var scene = new Scene { Name = "Scene" };
        var initial = new SceneEnvironmentData
        {
            AtmosphereEnabled = true,
            Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.1f, 0.2f, 0.3f) },
        };
        scene.SetEnvironment(initial);
        var recorder = new PropertyChangeRecorder(scene);

        var duplicateInstance = new SceneEnvironmentData
        {
            AtmosphereEnabled = true,
            Background = new BackgroundEnvironmentData { Enabled = true, ColorRgb = new Vector3(0.1f, 0.2f, 0.3f) },
        };
        scene.SetEnvironment(duplicateInstance);

        _ = recorder.ChangingCount(nameof(Scene.Environment)).Should().Be(0);
        _ = recorder.ChangedCount(nameof(Scene.Environment)).Should().Be(0);
    }

    [TestMethod]
    public void SetReferences_WhenValueDiffers_RaisesPropertyChangingAndPropertyChangedExactlyOnce()
    {
        var scene = new Scene { Name = "Scene" };
        var recorder = new PropertyChangeRecorder(scene);
        var references = new SceneReferencesData
        {
            Scripts = [new Uri("asset:///Content/Scripts/Test.oscript")],
        };

        scene.SetReferences(references);

        _ = recorder.ChangingCount(nameof(Scene.References)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(Scene.References)).Should().Be(1);
    }

    [TestMethod]
    public void SetReferences_WhenValueIsEqual_RaisesNoNotifications()
    {
        var scene = new Scene { Name = "Scene" };
        var references = new SceneReferencesData
        {
            Scripts = [new Uri("asset:///Content/Scripts/Test.oscript")],
        };
        scene.SetReferences(references);
        var recorder = new PropertyChangeRecorder(scene);

        var duplicateInstance = new SceneReferencesData
        {
            Scripts = [new Uri("asset:///Content/Scripts/Test.oscript")],
        };
        scene.SetReferences(duplicateInstance);

        _ = recorder.ChangingCount(nameof(Scene.References)).Should().Be(0);
        _ = recorder.ChangedCount(nameof(Scene.References)).Should().Be(0);
    }

    [TestMethod]
    public void SetStartingCamera_WhenValueDiffers_RaisesPropertyChangingAndPropertyChangedExactlyOnce()
    {
        var scene = new Scene { Name = "Scene" };
        var recorder = new PropertyChangeRecorder(scene);
        var cameraId = NodeId.New();

        scene.SetStartingCamera(cameraId);

        _ = scene.StartingCamera.Should().Be(cameraId);
        _ = recorder.ChangingCount(nameof(Scene.StartingCamera)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(Scene.StartingCamera)).Should().Be(1);
    }

    [TestMethod]
    public void SetStartingCamera_WhenValueIsEqual_RaisesNoNotifications()
    {
        var scene = new Scene { Name = "Scene" };
        var cameraId = NodeId.New();
        scene.SetStartingCamera(cameraId);
        var recorder = new PropertyChangeRecorder(scene);

        scene.SetStartingCamera(cameraId);

        _ = recorder.ChangingCount(nameof(Scene.StartingCamera)).Should().Be(0);
        _ = recorder.ChangedCount(nameof(Scene.StartingCamera)).Should().Be(0);
    }

    [TestMethod]
    public void SetStartingCamera_WithEmptyNodeId_StoresNull()
    {
        var scene = new Scene { Name = "Scene" };
        scene.SetStartingCamera(NodeId.New());

        scene.SetStartingCamera(NodeId.Empty);

        _ = scene.StartingCamera.Should().BeNull();
    }

    [TestMethod]
    public void SetStartingInputContexts_WhenValueDiffers_RaisesPropertyChangingAndPropertyChangedExactlyOnce()
    {
        var scene = new Scene { Name = "Scene" };
        var recorder = new PropertyChangeRecorder(scene);
        StartingInputContextData[] contexts = [new(new Uri("asset:///Content/Input/Move.oimap"), Priority: 2)];

        scene.SetStartingInputContexts(contexts);

        _ = recorder.ChangingCount(nameof(Scene.StartingInputContexts)).Should().Be(1);
        _ = recorder.ChangedCount(nameof(Scene.StartingInputContexts)).Should().Be(1);
    }

    [TestMethod]
    public void SetStartingInputContexts_WhenValueIsEqual_RaisesNoNotifications()
    {
        var scene = new Scene { Name = "Scene" };
        StartingInputContextData[] contexts = [new(new Uri("asset:///Content/Input/Move.oimap"), Priority: 2)];
        scene.SetStartingInputContexts(contexts);
        var recorder = new PropertyChangeRecorder(scene);

        StartingInputContextData[] sameContexts = [new(new Uri("asset:///Content/Input/Move.oimap"), Priority: 2)];
        scene.SetStartingInputContexts(sameContexts);

        _ = recorder.ChangingCount(nameof(Scene.StartingInputContexts)).Should().Be(0);
        _ = recorder.ChangedCount(nameof(Scene.StartingInputContexts)).Should().Be(0);
    }

    [TestMethod]
    public void EffectiveStartingCamera_WhenAuthoredNodeMissing_ReturnsNull()
    {
        var scene = new Scene { Name = "Scene" };
        var cameraNode = new SceneNode(scene) { Name = "Camera" };
        _ = cameraNode.AddComponent(new PerspectiveCamera { Name = "Camera" });
        scene.RootNodes.Add(cameraNode);
        var nonExistentNodeId = NodeId.New();

        scene.SetStartingCamera(nonExistentNodeId);

        _ = scene.EffectiveStartingCamera.Should().BeNull();
    }

    [TestMethod]
    public void EffectiveStartingCamera_WhenAuthoredNodeHasNoCameraComponent_ReturnsNull()
    {
        var scene = new Scene { Name = "Scene" };
        var nodeWithoutCamera = new SceneNode(scene) { Name = "NodeWithoutCamera" };
        scene.RootNodes.Add(nodeWithoutCamera);

        scene.SetStartingCamera(nodeWithoutCamera.Id);

        _ = scene.EffectiveStartingCamera.Should().BeNull();
    }

    [TestMethod]
    public void Dehydrate_Returns_SceneData_With_RootNodes()
    {
        // Arrange
        var scene = new Scene { Name = "Scene Name" };
        var node1 = new SceneNode(scene) { Name = "NodeValue 1" };
        var node2 = new SceneNode(scene) { Name = "NodeValue 2" };
        scene.RootNodes.Add(node1);
        scene.RootNodes.Add(node2);

        // Act
        var dto = scene.Dehydrate();

        // Assert - DTO shape and values (unit-level check, no JSON round-trip)
        _ = dto.Should().NotBeNull();
        _ = dto.Name.Should().Be("Scene Name");
        _ = dto.RootNodes.Should().HaveCount(2);
        _ = dto.RootNodes[0].Name.Should().Be("NodeValue 1");
        _ = dto.RootNodes[1].Name.Should().Be("NodeValue 2");
        _ = dto.Environment.Should().Be(new SceneEnvironmentData());
    }

    [TestMethod]
    public void CreateAndHydrate_Throws_When_Required_Properties_Missing()
    {
        // Arrange: Scene factory requires Name to be set by caller. Creating a DTO with missing Name
        // should still allow the factory to accept data, but CreateAndHydrate uses the DTO.Name as the scene
        // Name. We assert that creating a Scene with null Name is not allowed via object initializer.
        var data = new SceneData { Id = SceneId.New(), Name = null!, RootNodes = [] };

        // Act: factory CreateAndHydrate expects a non-null Name (the code assigns Name first). Simulate misuse.
        var act = () => Scene.CreateAndHydrate(data);

        // Assert - creating a scene with a null name should fail validation
        _ = act.Should().Throw<ArgumentException>();
    }
}
