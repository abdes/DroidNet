// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies Quick Add creation: placement, empty nodes and cameras.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly Vector3 PlacementPosition = new(3, 1.5f, -4);
    private static readonly Quaternion PlacementRotation = Quaternion.CreateFromAxisAngle(Vector3.UnitY, 0.6f);

    /// <summary>A placed primitive starts at the requested position as one undoable step.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CreatePrimitiveAsync_WithPlacement_PositionsTheNode()
    {
        var (fixture, scene, context) = CreatePlacementFixture();

        var result = await fixture.Sut.CreatePrimitiveAsync(context, "Cube", new NodePlacement(PlacementPosition)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = Transform(result.Value!).LocalPosition.Should().Be(PlacementPosition);
        _ = Transform(result.Value!).LocalRotation.Should().Be(Quaternion.Identity, "a placement without rotation keeps the default");
        _ = scene.RootNodes.Should().ContainSingle();
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>An empty node is a root with only its transform.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CreateEmptyNodeAsync_CreatesAPlacedRootWithOnlyATransform()
    {
        var (fixture, scene, context) = CreatePlacementFixture();

        var result = await fixture.Sut.CreateEmptyNodeAsync(context, new NodePlacement(PlacementPosition)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var node = scene.RootNodes.Should().ContainSingle().Which;
        _ = node.Should().BeSameAs(result.Value);
        _ = node.Components.Should().ContainSingle().Which.Should().BeOfType<TransformComponent>();
        _ = Transform(node).LocalPosition.Should().Be(PlacementPosition);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>Both camera kinds are created with their component at the requested pose.</summary>
    /// <param name="kind">The camera kind.</param>
    /// <param name="componentType">The camera component the kind creates.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Perspective", typeof(PerspectiveCamera))]
    [DataRow("Orthographic", typeof(OrthographicCamera))]
    public async Task CreateCameraAsync_CreatesTheCameraKindAtThePlacement(string kind, Type componentType)
    {
        var (fixture, _, context) = CreatePlacementFixture();

        var result = await fixture.Sut.CreateCameraAsync(context, kind, new NodePlacement(PlacementPosition, PlacementRotation)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var node = result.Value!;
        _ = node.Components.Should().ContainSingle(component => component.GetType() == componentType);
        _ = Transform(node).LocalPosition.Should().Be(PlacementPosition);
        _ = Transform(node).LocalRotation.Should().Be(PlacementRotation);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>An unknown camera kind fails visibly and leaves the scene untouched.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CreateCameraAsync_WhenKindIsUnknown_FailsVisiblyAndCreatesNoNode()
    {
        var (fixture, scene, context) = CreatePlacementFixture();

        var result = await fixture.Sut.CreateCameraAsync(context, "Fisheye").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.RootNodes.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.OperationKind.Should().Be(SceneOperationKinds.NodeCreateCamera);
    }

    private static TransformComponent Transform(SceneNode node) => node.Components.OfType<TransformComponent>().Single();

    private static (Fixture Fixture, Scene Scene, SceneDocumentCommandContext Context) CreatePlacementFixture()
    {
        var fixture = CreateFixture();
        _ = fixture.Sync
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), parentGuid: null))
            .Returns(Task.CompletedTask);
        var scene = CreateScene();
        return (fixture, scene, CreateContext(scene));
    }
}
