// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Documents;
using DroidNet.TimeMachine;
using Microsoft.UI;
using Moq;
using System.Numerics;
using Oxygen.Assets.Model;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneExplorer.Services;
using Oxygen.Editor.World.Serialization;
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
    public async Task EditTransformAsync_WhenOneShot_ShouldUsePropertyPipelineTransport()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var transform = node.Components.OfType<TransformComponent>().Single();
        var context = CreateContext(scene);
        IReadOnlyList<EnginePropertyValueEntry>? synced = null;
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, _, entries, _) => synced = entries)
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty));

        var result = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(4.0f)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = transform.LocalPosition.X.Should().Be(4.0f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().ContainSingle()
            .Which.Should().Be(new EnginePropertyValueEntry(EngineComponentId.Transform, (ushort)TransformField.PositionX, 4.0f));
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
        fixture.Sync.Verify(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()), Times.Never);
    }

    [TestMethod]
    public async Task EditTransformAsync_WhenOneShotValueIsUnchanged_ShouldNotDirtySyncOrRecordHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(0.0f)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                It.IsAny<Scene>(),
                It.IsAny<SceneNode>(),
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task EditTransformAsync_WhenPropertySyncIsSkipped_ShouldPublishLiveSyncDiagnostic()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var context = CreateContext(scene);
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(
                SyncStatus.SkippedNotRunning,
                SceneOperationKinds.EditTransform,
                AffectedScope.Empty,
                LiveSyncDiagnosticCodes.NotRunning,
                "The runtime engine is not running; live sync was skipped."));

        var result = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            new TransformEdit(
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                Optional<Vector3>.Unspecified,
                PositionX: Optional<float>.Supplied(4.0f)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(LiveSyncDiagnosticCodes.NotRunning);
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
        second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight = false;
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
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditDirectionalLightAsync_WhenEveryEditableFieldChanges_PersistsAndSyncsLight()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(node);
        var light = node.Components.OfType<DirectionalLightComponent>().Single();
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditDirectionalLight, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.AttachLightAsync(scene, node, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditDirectionalLightAsync(
            context,
            [node.Id],
            new DirectionalLightEdit(
                Optional<Vector3>.Supplied(new Vector3(0.25f, 0.5f, 0.75f)),
                Optional<float>.Supplied(45_000f),
                Optional<bool>.Supplied(false),
                Optional<bool>.Supplied(false),
                Optional<bool>.Supplied(true),
                Optional<bool>.Supplied(false),
                Optional<float>.Supplied(0.02f),
                Optional<float>.Supplied(1.25f)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = light.Color.Should().Be(new Vector3(0.25f, 0.5f, 0.75f));
        _ = light.IntensityLux.Should().Be(45_000f);
        _ = light.IsSunLight.Should().BeFalse();
        _ = light.EnvironmentContribution.Should().BeFalse();
        _ = light.CastsShadows.Should().BeTrue();
        _ = light.AffectsWorld.Should().BeFalse();
        _ = light.AngularSizeRadians.Should().Be(0.02f);
        _ = light.ExposureCompensation.Should().Be(1.25f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        fixture.Sync.Verify(sync => sync.AttachLightAsync(scene, node, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditSceneEnvironmentAsync_WhenSunIsBound_ClearsOtherSunLightsAndUndoRestores()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = CreateDirectionalLightNode(scene, "First");
        var second = CreateDirectionalLightNode(scene, "Second");
        second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight = false;
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        scene.SetEnvironment(new SceneEnvironmentData { SunNodeId = first.Id });
        var context = CreateContext(scene);
        var accepted = new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        fixture.Sync
            .Setup(sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditSceneEnvironmentAsync(
            context,
            new SceneEnvironmentEdit(
                Optional<bool>.Unspecified,
                Optional<Guid?>.Supplied(second.Id),
                Optional<ExposureMode>.Unspecified,
                Optional<float>.Unspecified,
                Optional<float>.Unspecified,
                Optional<ToneMappingMode>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.SunNodeId.Should().Be(second.Id);
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();

        await context.History.UndoAsync();
        _ = scene.Environment.SunNodeId.Should().Be(first.Id);
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();

        await context.History.RedoAsync();
        _ = scene.Environment.SunNodeId.Should().Be(second.Id);
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Exactly(3));
    }

    [TestMethod]
    public async Task EditSceneEnvironmentAsync_WhenSkyAtmosphereIsNotFinite_RejectsWithoutMutating()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var before = scene.Environment;
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditSceneEnvironmentAsync(
            context,
            new SceneEnvironmentEdit(
                Optional<bool>.Unspecified,
                Optional<Guid?>.Unspecified,
                Optional<ExposureMode>.Unspecified,
                Optional<float>.Unspecified,
                Optional<float>.Unspecified,
                Optional<ToneMappingMode>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<SkyAtmosphereEnvironmentData>.Supplied(new() { MieAnisotropy = float.NaN })),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.Environment.Should().Be(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(SceneDiagnosticCodes.EnvironmentSkyAtmosphereInvalid);
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
        fixture.DocumentService.Verify(
            service => service.UpdateMetadataAsync(It.IsAny<WindowId>(), context.DocumentId, context.Metadata),
            Times.Never);
    }

    [TestMethod]
    public async Task EditSceneEnvironmentAsync_WhenManualExposureIsEdited_PersistsAndSyncs()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        scene.SetEnvironment(new SceneEnvironmentData { ExposureMode = ExposureMode.Auto, ManualExposureEv = 9.7f });
        var context = CreateContext(scene);
        var accepted = new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        SceneEnvironmentData? syncedEnvironment = null;
        fixture.Sync
            .Setup(sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneEnvironmentData, CancellationToken>((_, environment, _) => syncedEnvironment = environment)
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditSceneEnvironmentAsync(
            context,
            new SceneEnvironmentEdit(
                Optional<bool>.Unspecified,
                Optional<Guid?>.Unspecified,
                Optional<ExposureMode>.Supplied(ExposureMode.Manual),
                Optional<float>.Supplied(3.5f),
                Optional<float>.Unspecified,
                Optional<ToneMappingMode>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.ExposureMode.Should().Be(ExposureMode.Manual);
        _ = scene.Environment.ManualExposureEv.Should().Be(3.5f);
        _ = scene.Environment.PostProcess.ExposureMode.Should().Be(ExposureMode.Manual);
        _ = scene.Environment.PostProcess.ManualExposureEv.Should().Be(3.5f);
        _ = syncedEnvironment.Should().NotBeNull();
        _ = syncedEnvironment!.ExposureMode.Should().Be(ExposureMode.Manual);
        _ = syncedEnvironment.ManualExposureEv.Should().Be(3.5f);
        _ = syncedEnvironment.PostProcess.ExposureMode.Should().Be(ExposureMode.Manual);
        _ = syncedEnvironment.PostProcess.ManualExposureEv.Should().Be(3.5f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task EditSceneEnvironmentAsync_WhenPostProcessIsEdited_PersistsNativePostProcessShapeAndSyncs()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var postProcess = new PostProcessEnvironmentData
        {
            ToneMapper = ToneMappingMode.Filmic,
            ExposureMode = ExposureMode.ManualCamera,
            ExposureEnabled = false,
            ExposureCompensationEv = 1.25f,
            ExposureKey = 11.0f,
            ManualExposureEv = 5.5f,
            AutoExposureMinEv = -3.0f,
            AutoExposureMaxEv = 14.0f,
            AutoExposureSpeedUp = 4.0f,
            AutoExposureSpeedDown = 2.0f,
            AutoExposureMeteringMode = MeteringMode.Spot,
            AutoExposureLowPercentile = 0.2f,
            AutoExposureHighPercentile = 0.8f,
            AutoExposureMinLogLuminance = -10.0f,
            AutoExposureLogLuminanceRange = 20.0f,
            AutoExposureTargetLuminance = 0.25f,
            AutoExposureSpotMeterRadius = 0.4f,
            BloomIntensity = 0.7f,
            BloomThreshold = 1.5f,
            Saturation = 0.9f,
            Contrast = 1.1f,
            VignetteIntensity = 0.3f,
            DisplayGamma = 2.4f,
        };
        var accepted = new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        SceneEnvironmentData? syncedEnvironment = null;
        fixture.Sync
            .Setup(sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneEnvironmentData, CancellationToken>((_, environment, _) => syncedEnvironment = environment)
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditSceneEnvironmentAsync(
            context,
            new SceneEnvironmentEdit(
                Optional<bool>.Unspecified,
                Optional<Guid?>.Unspecified,
                Optional<ExposureMode>.Unspecified,
                Optional<float>.Unspecified,
                Optional<float>.Unspecified,
                Optional<ToneMappingMode>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified,
                Optional<SkyAtmosphereEnvironmentData>.Unspecified,
                Optional<PostProcessEnvironmentData>.Supplied(postProcess)),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.PostProcess.Should().Be(postProcess);
        _ = scene.Environment.ExposureMode.Should().Be(postProcess.ExposureMode);
        _ = scene.Environment.ManualExposureEv.Should().Be(postProcess.ManualExposureEv);
        _ = scene.Environment.ExposureCompensation.Should().Be(postProcess.ExposureCompensationEv);
        _ = scene.Environment.ToneMapping.Should().Be(postProcess.ToneMapper);
        _ = syncedEnvironment.Should().NotBeNull();
        _ = syncedEnvironment!.PostProcess.Should().Be(postProcess);
    }

    [TestMethod]
    public async Task EditSceneEnvironmentAsync_WhenManualExposureIsNotFinite_RejectsWithoutMutating()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        scene.SetEnvironment(new SceneEnvironmentData { ManualExposureEv = 9.7f });
        var before = scene.Environment;
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditSceneEnvironmentAsync(
            context,
            new SceneEnvironmentEdit(
                Optional<bool>.Unspecified,
                Optional<Guid?>.Unspecified,
                Optional<ExposureMode>.Unspecified,
                Optional<float>.Supplied(float.NaN),
                Optional<float>.Unspecified,
                Optional<ToneMappingMode>.Unspecified,
                Optional<System.Numerics.Vector3>.Unspecified),
            EditSessionToken.OneShot);

        _ = result.Succeeded.Should().BeFalse();
        _ = scene.Environment.Should().Be(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = fixture.Results.Published.Should().ContainSingle()
            .Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(SceneDiagnosticCodes.EnvironmentManualExposureInvalid);
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
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
        fixture.Sync
            .Setup(sync => sync.UpdateNodeTransformAsync(scene, target, It.IsAny<CancellationToken>()))
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
