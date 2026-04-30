// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
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
using Oxygen.Editor.Schemas;
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
    public void ScenePropertyDescriptors_ShouldUseSceneSchemaOverlayAnnotations()
    {
        _ = SceneDocumentCommandService.Geometry.GeometryUriDescriptor.Annotation.Renderer.Should().Be("asset-picker");
        _ = SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegreesDescriptor.Annotation.Step.Should().Be(0.1);
        _ = SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegreesDescriptor.Annotation.Extra.Should().Contain("x-editor-unit", "deg");
        _ = SceneDocumentCommandService.DirectionalLight.IntensityLuxDescriptor.Annotation.Group.Should().Be("Emission");
        _ = SceneDocumentCommandService.DirectionalLight.IntensityLuxDescriptor.Annotation.Extra.Should().Contain("x-editor-unit", "lux");
        _ = SceneDocumentCommandService.SceneEnvironment.ById[SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters.Id]
            .Annotation.Extra.Should().Contain("x-editor-unit", "m");
    }

    [TestMethod]
    public async Task RemoveComponentAsync_WhenComponentIsTransform_DeniesAndKeepsAuthoringState()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var transform = node.Components.OfType<TransformComponent>().Single();
        var context = CreateContext(scene);

        var result = await fixture.Sut.RemoveComponentAsync(context, node.Id, transform.Id).ConfigureAwait(false);

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
            new GeometryEdit(OptionalEditValues.Supplied<Uri?>(null)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
        var geometryUri = AssetUris.BuildGeneratedUri("BasicShapes/Cube");
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(geometryUri),
        });
        var context = CreateContext(scene);

        var result = await fixture.Sut.EditGeometryAsync(
            context,
            [node.Id],
            new GeometryEdit(OptionalEditValues.Supplied<Uri?>(geometryUri)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
    public async Task EditPropertiesAsync_WhenGeometryUriChanges_PersistsAndSyncsGeometry()
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
        var sphereUri = AssetUris.BuildGeneratedUri("BasicShapes/Sphere");
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditGeometry, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.AttachGeometryAsync(scene, node, It.IsAny<CancellationToken>()))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditPropertiesAsync(
            context,
            [node.Id],
            PropertyEdit.Single(SceneDocumentCommandService.Geometry.GeometryUri, sphereUri),
            "Edit Geometry",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = geometry.Geometry.Should().NotBeNull();
        _ = geometry.Geometry!.Uri.Should().Be(sphereUri);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        fixture.Sync.Verify(sync => sync.AttachGeometryAsync(scene, node, It.IsAny<CancellationToken>()), Times.Once);
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
        var synced = new List<IReadOnlyList<EnginePropertyValueEntry>>();
        ConfigureTransformSessionPropertySync(fixture, scene, node, synced);
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [node.Id], "PositionX");

        _ = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            PositionXEdit(1f),
            session).ConfigureAwait(false);
        _ = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            PositionXEdit(2f),
            session).ConfigureAwait(false);

        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();

        session.Commit();
        var result = await fixture.Sut.EditTransformAsync(
            context,
            [node.Id],
            PositionXEdit(2f),
            session).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync().ConfigureAwait(false);
        _ = transform.LocalPosition.X.Should().Be(0f);
        _ = context.History.RedoStack.Should().ContainSingle();

        await context.History.RedoAsync().ConfigureAwait(false);
        _ = transform.LocalPosition.X.Should().Be(2f);
        _ = context.History.UndoStack.Should().ContainSingle();
        VerifyCommittedTransformSessionSync(fixture, scene, node, synced);
    }

    [TestMethod]
    public async Task EditTransformAsync_WhenSessionNodeIdsChange_ShouldKeepOriginalSessionNodes()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var firstTransform = first.Components.OfType<TransformComponent>().Single();
        var secondTransform = second.Components.OfType<TransformComponent>().Single();
        var context = CreateContext(scene);
        var synced = new List<IReadOnlyList<EnginePropertyValueEntry>>();
        ConfigureTransformSessionPropertySync(fixture, scene, first, synced);
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [first.Id], "PositionX");

        _ = await fixture.Sut.EditTransformAsync(context, [first.Id], PositionXEdit(1f), session).ConfigureAwait(false);
        _ = await fixture.Sut.EditTransformAsync(context, [second.Id], PositionXEdit(2f), session).ConfigureAwait(false);
        session.Commit();

        var result = await fixture.Sut.EditTransformAsync(context, [second.Id], PositionXEdit(2f), session).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = firstTransform.LocalPosition.X.Should().Be(2f);
        _ = secondTransform.LocalPosition.X.Should().Be(0f);
        _ = synced.Select(entries => entries.Should().ContainSingle().Which.Value)
            .Should().Equal(1f, 2f, 2f);
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                second,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Never);
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
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                PositionX: OptionalEditValues.Supplied<float>(4.0f)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                PositionX: OptionalEditValues.Supplied<float>(0.0f)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                OptionalEditValues.Unspecified<Vector3>(),
                PositionX: OptionalEditValues.Supplied<float>(4.0f)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.ToString().Should().Be("asset:///__uninitialized__");
        _ = context.Metadata.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenMaterialSlotIsCleared_SyncsNullMaterialUri()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var materialUri = AssetUris.BuildGeneratedUri("Materials/Matte");
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

        var result = await fixture.Sut.EditPropertiesAsync(
            context,
            [node.Id],
            PropertyEdit.Single(SceneDocumentCommandService.Geometry.MaterialSlot0Uri, (Uri?)null),
            "Edit Material Slot",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.ToString().Should().Be("asset:///__uninitialized__");
        fixture.Sync.Verify(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, null, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(
            sync => sync.UpdateMaterialSlotAsync(scene, node, 0, new Uri("asset:///__uninitialized__"), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditMaterialSlotAsync_WhenMaterialIsUnchanged_DoesNotDirtyOrRecordHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(node);
        var materialUri = AssetUris.BuildGeneratedUri("Materials/Matte");
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
            EditSessionToken.OneShot).ConfigureAwait(false);

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
        var materialUri = AssetUris.BuildGeneratedUri("Materials/Matte");
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
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();

        await context.History.UndoAsync().ConfigureAwait(false);
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.Should().Be(materialUri);

        await context.History.RedoAsync().ConfigureAwait(false);
        _ = geometry.OverrideSlots.OfType<MaterialsSlot>().Should().ContainSingle()
            .Which.Material.Uri.ToString().Should().Be("asset:///__uninitialized__");
        fixture.Sync.Verify(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, null, It.IsAny<CancellationToken>()), Times.Exactly(2));
        fixture.Sync.Verify(sync => sync.UpdateMaterialSlotAsync(scene, node, 0, materialUri, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(
            sync => sync.UpdateMaterialSlotAsync(scene, node, 0, new Uri("asset:///__uninitialized__"), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenPerspectiveCameraPropertiesChange_PersistsAndSyncsCamera()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Camera" };
        scene.RootNodes.Add(node);
        var camera = new PerspectiveCamera
        {
            Name = "Camera",
            FieldOfView = 60f,
            NearPlane = 0.1f,
            FarPlane = 1000f,
            AspectRatio = 16f / 9f,
        };
        _ = node.AddComponent(camera);
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditPerspectiveCamera, AffectedScope.Empty);
        IReadOnlyList<EnginePropertyValueEntry>? synced = null;
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, _, entries, _) => synced = entries)
            .ReturnsAsync(accepted);

        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.PerspectiveCamera.FieldOfViewDegrees, 75f);
        edit.Set(SceneDocumentCommandService.PerspectiveCamera.AspectRatio, 1.5f);
        edit.Set(SceneDocumentCommandService.PerspectiveCamera.NearPlane, 0.25f);
        edit.Set(SceneDocumentCommandService.PerspectiveCamera.FarPlane, 2500f);

        var result = await fixture.Sut.EditPropertiesAsync(
            context,
            [node.Id],
            edit,
            "Edit Camera",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = camera.FieldOfView.Should().Be(75f);
        _ = camera.AspectRatio.Should().Be(1.5f);
        _ = camera.NearPlane.Should().Be(0.25f);
        _ = camera.FarPlane.Should().Be(2500f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().NotBeNull();
        _ = synced!.Should().Contain(entry => entry.Component == EngineComponentId.PerspectiveCamera && entry.FieldId == (ushort)PerspectiveCameraField.FieldOfViewYRadians);
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
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
        var synced = new List<(SceneNode Node, IReadOnlyList<EnginePropertyValueEntry> Entries)>();
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                It.IsAny<SceneNode>(),
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, node, entries, _) => synced.Add((node, entries)))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditDirectionalLightAsync(
            context,
            [second.Id],
            new DirectionalLightEdit(
                OptionalEditValues.Unspecified<System.Numerics.Vector3>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Supplied<bool>(true),
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>()),
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().HaveCount(2);
        _ = synced.Should().Contain(entry => entry.Node == first && entry.Entries.Contains(new EnginePropertyValueEntry(EngineComponentId.DirectionalLight, (ushort)DirectionalLightField.IsSunLight, 0f)));
        _ = synced.Should().Contain(entry => entry.Node == second && entry.Entries.Contains(new EnginePropertyValueEntry(EngineComponentId.DirectionalLight, (ushort)DirectionalLightField.IsSunLight, 1f)));
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                It.IsAny<SceneNode>(),
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Exactly(2));
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenDirectionalLightBecomesSun_PreservesExclusiveSunBehavior()
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
        var synced = new List<(SceneNode Node, IReadOnlyList<EnginePropertyValueEntry> Entries)>();
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                It.IsAny<SceneNode>(),
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, node, entries, _) => synced.Add((node, entries)))
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditPropertiesAsync(
            context,
            [second.Id],
            PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.IsSunLight, true),
            "Edit Directional Light",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().HaveCount(2);
        _ = synced.Should().Contain(entry => entry.Node == first && entry.Entries.Contains(new EnginePropertyValueEntry(EngineComponentId.DirectionalLight, (ushort)DirectionalLightField.IsSunLight, 0f)));
        _ = synced.Should().Contain(entry => entry.Node == second && entry.Entries.Contains(new EnginePropertyValueEntry(EngineComponentId.DirectionalLight, (ushort)DirectionalLightField.IsSunLight, 1f)));
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                It.IsAny<SceneNode>(),
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Exactly(2));
        fixture.Sync.Verify(
            sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()),
            Times.Never);
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenDirectionalLightPropertiesChange_PersistsAndSyncsLight()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = CreateDirectionalLightNode(scene, "Sun");
        scene.RootNodes.Add(node);
        var light = node.Components.OfType<DirectionalLightComponent>().Single();
        var context = CreateContext(scene);
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditDirectionalLight, AffectedScope.Empty);
        IReadOnlyList<EnginePropertyValueEntry>? synced = null;
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, _, entries, _) => synced = entries)
            .ReturnsAsync(accepted);

        var edit = BuildDirectionalLightPropertyEdit();

        var result = await fixture.Sut.EditPropertiesAsync(
            context,
            [node.Id],
            edit,
            "Edit Directional Light",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        AssertDirectionalLightValues(
            light,
            color: new Vector3(0.2f, 0.4f, 0.8f),
            intensityLux: 12_000f,
            angularSizeRadians: 0.0125f,
            exposureCompensation: 0.75f,
            mobility: LightMobility.Mixed,
            shadowResolutionHint: ShadowResolutionHint.High,
            cascadeCount: 3,
            maxShadowDistance: 256f,
            cascadeDistances: new Vector4(16f, 48f, 128f, 256f),
            distributionExponent: 2.25f,
            transitionFraction: 0.2f,
            distanceFadeoutFraction: 0.15f,
            shadowBias: 0.001f,
            shadowNormalBias: 0.04f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().NotBeNull();
        _ = synced!.Should().Contain(entry => entry.Component == EngineComponentId.DirectionalLight && entry.FieldId == (ushort)DirectionalLightField.IntensityLux);
        _ = synced!.Should().Contain(entry => entry.Component == EngineComponentId.DirectionalLight && entry.FieldId == (ushort)DirectionalLightField.CascadeDistance3);
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
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
        IReadOnlyList<EnginePropertyValueEntry>? synced = null;
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, _, entries, _) => synced = entries)
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditDirectionalLightAsync(
            context,
            [node.Id],
            BuildDirectionalLightEdit(),
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        AssertDirectionalLightValues(
            light,
            color: new Vector3(0.25f, 0.5f, 0.75f),
            intensityLux: 45_000f,
            angularSizeRadians: 0.02f,
            exposureCompensation: 1.25f,
            mobility: LightMobility.Baked,
            shadowResolutionHint: ShadowResolutionHint.Ultra,
            cascadeCount: 2,
            maxShadowDistance: 512f,
            cascadeDistances: new Vector4(32f, 128f, 256f, 512f),
            distributionExponent: 1.5f,
            transitionFraction: 0.25f,
            distanceFadeoutFraction: 0.3f,
            shadowBias: 0.002f,
            shadowNormalBias: 0.05f);
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().NotBeNull();
        _ = synced!.Should().Contain(entry => entry.Component == EngineComponentId.DirectionalLight && entry.FieldId == (ushort)DirectionalLightField.IntensityLux);
        _ = synced!.Should().Contain(entry => entry.Component == EngineComponentId.DirectionalLight && entry.FieldId == (ushort)DirectionalLightField.DistanceFadeoutFraction);
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Once);
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
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Supplied<Guid?>(second.Id),
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<System.Numerics.Vector3>()),
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.SunNodeId.Should().Be(second.Id);
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();

        await context.History.UndoAsync().ConfigureAwait(false);
        _ = scene.Environment.SunNodeId.Should().Be(first.Id);
        _ = first.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = second.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeFalse();

        await context.History.RedoAsync().ConfigureAwait(false);
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
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<Guid?>(),
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<System.Numerics.Vector3>(),
                OptionalEditValues.Supplied<SkyAtmosphereEnvironmentData>(new() { MieAnisotropy = float.NaN })),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<Guid?>(),
                OptionalEditValues.Supplied<ExposureMode>(ExposureMode.Manual),
                OptionalEditValues.Supplied<float>(3.5f),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<System.Numerics.Vector3>()),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<Guid?>(),
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<System.Numerics.Vector3>(),
                OptionalEditValues.Unspecified<SkyAtmosphereEnvironmentData>(),
                OptionalEditValues.Supplied<PostProcessEnvironmentData>(postProcess)),
            EditSessionToken.OneShot).ConfigureAwait(false);

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
    public async Task EditSceneEnvironmentPropertiesAsync_WhenPostProcessPropertiesChange_PersistsAndSyncs()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var expected = CreatePostProcessEnvironmentData();
        scene.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { ExposureKey = 0.18f } });
        var context = CreateContext(scene);
        var accepted = new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        SceneEnvironmentData? syncedEnvironment = null;
        fixture.Sync
            .Setup(sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneEnvironmentData, CancellationToken>((_, environment, _) => syncedEnvironment = environment)
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            BuildPostProcessPropertyEdit(expected),
            "Edit Environment",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        VerifyPostProcessEnvironment(scene.Environment, expected);
        _ = syncedEnvironment.Should().NotBeNull();
        VerifyPostProcessEnvironment(syncedEnvironment!, expected);
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task EditSceneEnvironmentPropertiesAsync_WhenSkyPropertiesChange_PersistsAndSyncs()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var expected = CreateSkyAtmosphereEnvironmentData();
        scene.SetEnvironment(new SceneEnvironmentData { SkyAtmosphere = new SkyAtmosphereEnvironmentData { MieAnisotropy = 0.45f } });
        var context = CreateContext(scene);
        var accepted = new EnvironmentSyncResult(SyncStatus.Accepted, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        SceneEnvironmentData? syncedEnvironment = null;
        fixture.Sync
            .Setup(sync => sync.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneEnvironmentData, CancellationToken>((_, environment, _) => syncedEnvironment = environment)
            .ReturnsAsync(accepted);

        var result = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            BuildSkyAtmospherePropertyEdit(expected),
            "Edit Environment",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.SkyAtmosphere.Should().Be(expected);
        _ = syncedEnvironment.Should().NotBeNull();
        _ = syncedEnvironment!.SkyAtmosphere.Should().Be(expected);
        _ = context.History.UndoStack.Should().ContainSingle();
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
                OptionalEditValues.Unspecified<bool>(),
                OptionalEditValues.Unspecified<Guid?>(),
                OptionalEditValues.Unspecified<ExposureMode>(),
                OptionalEditValues.Supplied<float>(float.NaN),
                OptionalEditValues.Unspecified<float>(),
                OptionalEditValues.Unspecified<ToneMappingMode>(),
                OptionalEditValues.Unspecified<System.Numerics.Vector3>()),
            EditSessionToken.OneShot).ConfigureAwait(false);

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

        var result = await fixture.Sut.AddComponentAsync(context, target.Id, typeof(DirectionalLightComponent)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().BeOfType<DirectionalLightComponent>()
            .Which.IsSunLight.Should().BeFalse();
        _ = existingSun.Components.OfType<DirectionalLightComponent>().Single().IsSunLight.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    private static PostProcessEnvironmentData CreatePostProcessEnvironmentData()
        => new()
        {
            ExposureMode = ExposureMode.ManualCamera,
            ExposureEnabled = false,
            ManualExposureEv = 4.25f,
            ExposureCompensationEv = 1.5f,
            ExposureKey = 0.33f,
            ToneMapper = ToneMappingMode.Filmic,
            AutoExposureMeteringMode = MeteringMode.Spot,
            AutoExposureMinEv = -4f,
            AutoExposureMaxEv = 12f,
            AutoExposureSpeedUp = 3f,
            AutoExposureSpeedDown = 1.5f,
            AutoExposureLowPercentile = 0.1f,
            AutoExposureHighPercentile = 0.9f,
            AutoExposureMinLogLuminance = -9f,
            AutoExposureLogLuminanceRange = 18f,
            AutoExposureTargetLuminance = 0.5f,
            AutoExposureSpotMeterRadius = 0.3f,
            BloomIntensity = 0.75f,
            BloomThreshold = 1.4f,
            Saturation = 0.8f,
            Contrast = 1.15f,
            VignetteIntensity = 0.35f,
            DisplayGamma = 2.2f,
        };

    private static SkyAtmosphereEnvironmentData CreateSkyAtmosphereEnvironmentData()
        => new()
        {
            PlanetRadiusMeters = 6_360_000f,
            AtmosphereHeightMeters = 100_000f,
            GroundAlbedoRgb = new Vector3(0.1f, 0.2f, 0.3f),
            RayleighScaleHeightMeters = 8000f,
            MieScaleHeightMeters = 1200f,
            MieAnisotropy = 0.7f,
            SkyLuminanceFactorRgb = new Vector3(1.1f, 0.9f, 0.8f),
            AerialPerspectiveDistanceScale = 0.35f,
            AerialScatteringStrength = 0.6f,
            AerialPerspectiveStartDepthMeters = 100f,
            HeightFogContribution = 0.25f,
            SunDiskEnabled = false,
        };

    private static PropertyEdit BuildPostProcessPropertyEdit(PostProcessEnvironmentData value)
    {
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ExposureMode, value.ExposureMode);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled, value.ExposureEnabled);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv, value.ManualExposureEv);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation, value.ExposureCompensationEv);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ExposureKey, value.ExposureKey);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.ToneMapping, value.ToneMapper);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode, value.AutoExposureMeteringMode);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv, value.AutoExposureMinEv);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv, value.AutoExposureMaxEv);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp, value.AutoExposureSpeedUp);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown, value.AutoExposureSpeedDown);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile, value.AutoExposureLowPercentile);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile, value.AutoExposureHighPercentile);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance, value.AutoExposureMinLogLuminance);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange, value.AutoExposureLogLuminanceRange);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance, value.AutoExposureTargetLuminance);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius, value.AutoExposureSpotMeterRadius);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.BloomIntensity, value.BloomIntensity);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.BloomThreshold, value.BloomThreshold);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.Saturation, value.Saturation);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.Contrast, value.Contrast);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.VignetteIntensity, value.VignetteIntensity);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.DisplayGamma, value.DisplayGamma);
        return edit;
    }

    private static PropertyEdit BuildSkyAtmospherePropertyEdit(SkyAtmosphereEnvironmentData value)
    {
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters, value.PlanetRadiusMeters);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters, value.AtmosphereHeightMeters);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, value.GroundAlbedoRgb);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters, value.RayleighScaleHeightMeters);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters, value.MieScaleHeightMeters);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy, value.MieAnisotropy);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, value.SkyLuminanceFactorRgb);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale, value.AerialPerspectiveDistanceScale);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength, value.AerialScatteringStrength);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters, value.AerialPerspectiveStartDepthMeters);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution, value.HeightFogContribution);
        edit.Set(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled, value.SunDiskEnabled);
        return edit;
    }

    private static void VerifyPostProcessEnvironment(SceneEnvironmentData environment, PostProcessEnvironmentData value)
    {
        _ = environment.ExposureMode.Should().Be(value.ExposureMode);
        _ = environment.ManualExposureEv.Should().Be(value.ManualExposureEv);
        _ = environment.ExposureCompensation.Should().Be(value.ExposureCompensationEv);
        _ = environment.ToneMapping.Should().Be(value.ToneMapper);
        _ = environment.PostProcess.Should().Be(value);
    }

    private static PropertyEdit BuildDirectionalLightPropertyEdit()
    {
        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.DirectionalLight.Color, new Vector3(0.2f, 0.4f, 0.8f));
        edit.Set(SceneDocumentCommandService.DirectionalLight.IntensityLux, 12_000f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.IsSunLight, false);
        edit.Set(SceneDocumentCommandService.DirectionalLight.EnvironmentContribution, false);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CastsShadows, true);
        edit.Set(SceneDocumentCommandService.DirectionalLight.AffectsWorld, false);
        edit.Set(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians, 0.0125f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.ExposureCompensation, 0.75f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.Mobility, LightMobility.Mixed);
        edit.Set(SceneDocumentCommandService.DirectionalLight.ShadowBias, 0.001f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.ShadowNormalBias, 0.04f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.ContactShadows, true);
        edit.Set(SceneDocumentCommandService.DirectionalLight.ShadowResolutionHint, ShadowResolutionHint.High);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeCount, 3);
        edit.Set(SceneDocumentCommandService.DirectionalLight.SplitMode, DirectionalCsmSplitMode.ManualDistances);
        edit.Set(SceneDocumentCommandService.DirectionalLight.MaxShadowDistance, 256f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeDistance0, 16f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeDistance1, 48f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeDistance2, 128f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.CascadeDistance3, 256f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.DistributionExponent, 2.25f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.TransitionFraction, 0.2f);
        edit.Set(SceneDocumentCommandService.DirectionalLight.DistanceFadeoutFraction, 0.15f);
        return edit;
    }

    private static DirectionalLightEdit BuildDirectionalLightEdit()
        => new(
            Color: OptionalEditValues.Supplied<Vector3>(new Vector3(0.25f, 0.5f, 0.75f)),
            IntensityLux: OptionalEditValues.Supplied<float>(45_000f),
            IsSunLight: OptionalEditValues.Supplied<bool>(false),
            EnvironmentContribution: OptionalEditValues.Supplied<bool>(false),
            CastsShadows: OptionalEditValues.Supplied<bool>(true),
            AffectsWorld: OptionalEditValues.Supplied<bool>(false),
            AngularSizeRadians: OptionalEditValues.Supplied<float>(0.02f),
            ExposureCompensation: OptionalEditValues.Supplied<float>(1.25f),
            Mobility: OptionalEditValues.Supplied<LightMobility>(LightMobility.Baked),
            ShadowBias: OptionalEditValues.Supplied<float>(0.002f),
            ShadowNormalBias: OptionalEditValues.Supplied<float>(0.05f),
            ContactShadows: OptionalEditValues.Supplied<bool>(true),
            ShadowResolutionHint: OptionalEditValues.Supplied<ShadowResolutionHint>(ShadowResolutionHint.Ultra),
            CascadeCount: OptionalEditValues.Supplied<int>(2),
            SplitMode: OptionalEditValues.Supplied<DirectionalCsmSplitMode>(DirectionalCsmSplitMode.ManualDistances),
            MaxShadowDistance: OptionalEditValues.Supplied<float>(512f),
            CascadeDistance0: OptionalEditValues.Supplied<float>(32f),
            CascadeDistance1: OptionalEditValues.Supplied<float>(128f),
            CascadeDistance2: OptionalEditValues.Supplied<float>(256f),
            CascadeDistance3: OptionalEditValues.Supplied<float>(512f),
            DistributionExponent: OptionalEditValues.Supplied<float>(1.5f),
            TransitionFraction: OptionalEditValues.Supplied<float>(0.25f),
            DistanceFadeoutFraction: OptionalEditValues.Supplied<float>(0.3f));

    private static void AssertDirectionalLightValues(
        DirectionalLightComponent light,
        Vector3 color,
        float intensityLux,
        float angularSizeRadians,
        float exposureCompensation,
        LightMobility mobility,
        ShadowResolutionHint shadowResolutionHint,
        int cascadeCount,
        float maxShadowDistance,
        Vector4 cascadeDistances,
        float distributionExponent,
        float transitionFraction,
        float distanceFadeoutFraction,
        float shadowBias,
        float shadowNormalBias)
    {
        _ = light.Color.Should().Be(color);
        _ = light.IntensityLux.Should().Be(intensityLux);
        _ = light.IsSunLight.Should().BeFalse();
        _ = light.EnvironmentContribution.Should().BeFalse();
        _ = light.CastsShadows.Should().BeTrue();
        _ = light.AffectsWorld.Should().BeFalse();
        _ = light.AngularSizeRadians.Should().Be(angularSizeRadians);
        _ = light.ExposureCompensation.Should().Be(exposureCompensation);
        _ = light.Mobility.Should().Be(mobility);
        _ = light.ShadowBias.Should().Be(shadowBias);
        _ = light.ShadowNormalBias.Should().Be(shadowNormalBias);
        _ = light.ContactShadows.Should().BeTrue();
        _ = light.ShadowResolutionHint.Should().Be(shadowResolutionHint);
        _ = light.CascadeCount.Should().Be(cascadeCount);
        _ = light.SplitMode.Should().Be(DirectionalCsmSplitMode.ManualDistances);
        _ = light.MaxShadowDistance.Should().Be(maxShadowDistance);
        _ = light.CascadeDistances.Should().Be(cascadeDistances);
        _ = light.DistributionExponent.Should().Be(distributionExponent);
        _ = light.TransitionFraction.Should().Be(transitionFraction);
        _ = light.DistanceFadeoutFraction.Should().Be(distanceFadeoutFraction);
    }

    private static SceneNode CreateDirectionalLightNode(Scene scene, string name)
    {
        var node = new SceneNode(scene) { Name = name };
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Directional Light", IsSunLight = true });
        return node;
    }

    private static void VerifyCommittedTransformSessionSync(
        Fixture fixture,
        Scene scene,
        SceneNode node,
        IEnumerable<IReadOnlyList<EnginePropertyValueEntry>> synced)
    {
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
        fixture.Sync.Verify(
            sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()),
            Times.Exactly(5));
        _ = synced.Select(entries => entries.Should().ContainSingle().Which.Value)
            .Should().Equal(1f, 2f, 2f, 0f, 2f);
        _ = synced.Select(entries => entries.Should().ContainSingle().Which.FieldId)
            .Should().OnlyContain(field => field == (ushort)TransformField.PositionX);
        fixture.Sync.Verify(sync => sync.UpdateNodeTransformAsync(scene, node, It.IsAny<CancellationToken>()), Times.Never);
    }

    private static TransformEdit PositionXEdit(float value)
        => new(
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            OptionalEditValues.Unspecified<Vector3>(),
            PositionX: OptionalEditValues.Supplied<float>(value));

    private static void ConfigureTransformSessionPropertySync(
        Fixture fixture,
        Scene scene,
        SceneNode node,
        List<IReadOnlyList<EnginePropertyValueEntry>> synced)
    {
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditTransform, AffectedScope.Empty);
        fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(
                scene,
                node,
                It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(),
                It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, CancellationToken>((_, _, entries, _) => synced.Add(entries))
            .ReturnsAsync(accepted);
        fixture.Sync
            .Setup(sync => sync.TryPreviewSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<DateTimeOffset>(),
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()))
            .Returns<Guid, Guid, DateTimeOffset, Func<CancellationToken, Task<SyncOutcome>>, CancellationToken>(
                async (_, _, _, sync, cancellationToken) => (SyncOutcome?)await sync(cancellationToken).ConfigureAwait(false));
        fixture.Sync
            .Setup(sync => sync.CompleteTerminalSyncAsync(
                scene.Id,
                node.Id,
                It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(),
                It.IsAny<CancellationToken>()))
            .Returns<Guid, Guid, Func<CancellationToken, Task<SyncOutcome>>, CancellationToken>(
                (_, _, sync, cancellationToken) => sync(cancellationToken));
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
