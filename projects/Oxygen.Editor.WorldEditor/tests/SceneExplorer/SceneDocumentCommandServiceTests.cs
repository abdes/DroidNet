// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using Oxygen.Assets.Model;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
[TestCategory("Scene Commands")]
public sealed class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task RemoveComponentAsync_WhenComponentIsTransform_DeniesAndKeepsAuthoringState()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var transform = node.Components.OfType<TransformComponent>().Single();
        var context = CreateContext(scene);

        var result = await fixture.Sut.RemoveComponentAsync(context, node.Id, transform.Id);

        _ = result.Succeeded.Should().BeFalse();
        _ = node.Components.Should().Contain(transform);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(SceneDiagnosticCodes.ComponentRemoveDenied);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditGeometryAsync_WhenGeometryIsCleared_RejectsUnsavableState()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        });
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditGeometryAsync(
            context,
            [node.Id],
            new GeometryEdit(Optional<Uri?>.Supplied(null)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeFalse();
        _ = node.Components.OfType<GeometryComponent>().Single().Geometry.Should().NotBeNull();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(SceneDiagnosticCodes.GeometryReferenceRequired);
        fixture.Sync.Verify(sync => sync.DetachGeometryAsync(scene, node.Id, It.IsAny<CancellationToken>()), Times.Never);
        fixture.Sync.Verify(sync => sync.AttachGeometryAsync(scene, node, It.IsAny<CancellationToken>()), Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task EditGeometryAsync_WhenAssetIsUnchanged_DoesNotDirtyOrRecordHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var geometryUri = new Uri(AssetUris.BuildGeneratedUri("BasicShapes/Cube"), UriKind.Absolute);
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(geometryUri),
        });
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditGeometryAsync(
            context,
            [node.Id],
            new GeometryEdit(Optional<Uri?>.Supplied(geometryUri)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        fixture.Sync.Verify(sync => sync.AttachGeometryAsync(scene, node, It.IsAny<CancellationToken>()), Times.Never);
        fixture.Sync.Verify(sync => sync.DetachGeometryAsync(scene, node.Id, It.IsAny<CancellationToken>()), Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task EditTransformAsync_WhenSessionCommits_RecordsSingleUndoEntry()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var transform = node.Components.OfType<TransformComponent>().Single();
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.TryPreviewSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<DateTimeOffset>(),
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);
        fixture.Sync
            .Setup(sync => sync.CompleteTerminalSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);
        fixture.Sync
            .Setup(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [node.Id], "PositionX");

        _ = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(1f)),
            session);
        _ = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(2f)),
            session);

        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();

        session.Commit();
        var result = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(2f)),
            session);

        _ = result.Succeeded.Should().BeTrue();
        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync();
        _ = transform.LocalPosition.X.Should().Be(0f);
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync();
        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.History.UndoStack.Should().ContainSingle();
        fixture.Sync.Verify(
            sync => sync.TryPreviewSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<DateTimeOffset>(),
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()),
            Times.Exactly(2));
        fixture.Sync.Verify(
            sync => sync.CompleteTerminalSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
        fixture.Sync.Verify(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()), Times.Exactly(2));
    }

    [TestMethod]
    public async Task EditMaterialSlotAsync_WhenCleared_PersistsEmptySentinelSlot()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        _ = node.AddComponent(geometry);
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditMaterialSlot, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, null, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditMaterialSlotAsync(
            context,
            [node.Id],
            slotIndex: 0,
            newMaterialUri: null,
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.ToString().Should().Be("asset:///__uninitialized__");
        _ = context.Metadata.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task EditMaterialSlotAsync_WhenMaterialIsUnchanged_DoesNotDirtyOrRecordHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var materialUri = new Uri(AssetUris.BuildGeneratedUri("Materials/Matte"), UriKind.Absolute);
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        geometry.OverrideSlots.Add(new MaterialsSlot { Material = new AssetReference<MaterialAsset>(materialUri) });
        _ = node.AddComponent(geometry);
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditMaterialSlotAsync(
            context,
            [node.Id],
            slotIndex: 0,
            newMaterialUri: materialUri,
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        fixture.Sync.Verify(
            sync => sync.UpdateMaterialSlotAsync(scene, node, 0, It.IsAny<Uri?>(), It.IsAny<CancellationToken>()),
            Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task EditMaterialSlotAsync_WhenClearedUndoRedo_SyncsNullInsteadOfEmptySentinel()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var materialUri = new Uri(AssetUris.BuildGeneratedUri("Materials/Matte"), UriKind.Absolute);
        var geometry = new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri("BasicShapes/Cube")),
        };
        geometry.OverrideSlots.Add(new MaterialsSlot { Material = new AssetReference<MaterialAsset>(materialUri) });
        _ = node.AddComponent(geometry);
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditMaterialSlot, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, null, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);
        fixture.Sync
            .Setup(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, materialUri, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditMaterialSlotAsync(
            context,
            [node.Id],
            slotIndex: 0,
            newMaterialUri: null,
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync();
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.Should().Be(materialUri);

        await context.History.RedoAsync();
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.ToString().Should().Be("asset:///__uninitialized__");
        fixture.Sync.Verify(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, null, It.IsAny<CancellationToken>()), Times.Exactly(2));
        fixture.Sync.Verify(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, materialUri, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(
            sync => sync.UpdateMaterialSlotAsync(scene, node, 0, new Uri("asset:///__uninitialized__"), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditDirectionalLightAsync_WhenLightBecomesSun_ClearsOtherSunLights()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CreateDirectionalLightNode(scene, "First");
        var second = CreateDirectionalLightNode(scene, "Second");
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditDirectionalLight, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.AttachLightAsync(scene, first, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);
        fixture.Sync
            .Setup(sync => sync.AttachLightAsync(scene, second, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditDirectionalLightAsync(
            context,
            [second.Id],
            new DirectionalLightEdit(
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<float>.Unspecified,
                Optional<bool>.Supplied(true),
                Optional<bool>.Unspecified,
                Optional<bool>.Unspecified,
                Optional<bool>.Unspecified,
                Optional<float>.Unspecified,
                Optional<float>.Unspecified),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        fixture.Sync.Verify(sync => sync.AttachLightAsync(scene, first, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(sync => sync.AttachLightAsync(scene, second, It.IsAny<CancellationToken>()), Times.Once);
    }

    [TestMethod]
    public async Task AddComponentAsync_WhenAddingDirectionalLight_DoesNotStealSunBinding()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var existingSun = CreateDirectionalLightNode(scene, "Sun");
        var target = new SceneNode(scene) { Name = "Target" };
        scene.RootNodes.Add(existingSun);
        scene.RootNodes.Add(target);
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.AddComponent, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.AttachLightAsync(scene, target, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.AddComponentAsync(context, target.Id, typeof(DirectionalLightComponent));

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().BeOfType<DirectionalLightComponent>()
            .Which.IsSunLight.Should().BeFalse();
        _ = existingSun.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    private static SceneNode CreateDirectionalLightNode(Scene scene, string name)
    {
        var node = new SceneNode(scene) { Name = name };
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Directional Light", IsSunLight = true });
        return node;
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }

    private static SceneDocumentCommandContext CreateContext(Scene scene)
    {
        var metadata = new SceneDocumentMetadata { Title = scene.Name };
        return new(metadata.DocumentId, metadata, scene, new HistoryKeeper(scene));
    }

    private static Fixture CreateFixture()
    {
        var sync = new Mock<ISceneEngineSync>(MockBehavior.Strict);
        var documentService = new Mock<IDocumentService>(MockBehavior.Strict);
        documentService
            .Setup(service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()))
            .ReturnsAsync(true);
        var results = new CapturingOperationResultPublisher();
        var sut = new SceneDocumentCommandService(
            new Mock<ISceneExplorerService>(MockBehavior.Strict).Object,
            new SceneSelectionService(),
            sync.Object,
            new Mock<IProjectManagerService>(MockBehavior.Strict).Object,
            documentService.Object,
            default,
            WeakReferenceMessenger.Default,
            results,
            new OperationStatusReducer());

        return new(sut, sync, documentService, results);
    }

    private sealed record Fixture(
        SceneDocumentCommandService Sut,
        Mock<ISceneEngineSync> Sync,
        Mock<IDocumentService> DocumentService,
        CapturingOperationResultPublisher Results);

    private sealed class CapturingOperationResultPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoopDisposable();
    }

    private sealed class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
