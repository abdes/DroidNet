// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Node rendering flags, orthographic cameras and point/spot lights edited through descriptors.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task EditPropertiesAsync_WhenNodeRenderingFlagsChange_SyncsLocalFlagsAndUndoes()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var parent = new SceneNode(scene) { Name = "Parent", IsVisible = true, CastsShadows = true };
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);
        var synced = ConfigureDescriptorSync(fixture, scene);

        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.NodeRendering.IsVisible, false);
        edit.Set(SceneDocumentCommandService.NodeRendering.CastsShadows, false);
        var result = await fixture.Sut.EditPropertiesAsync(context, [parent.Id], edit, "Edit Rendering", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = parent.IsVisible.Should().BeFalse();
        _ = parent.CastsShadows.Should().BeFalse();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().ContainSingle().Which.Should().BeEquivalentTo(
        [
            new EnginePropertyValueEntry(EngineComponentId.Node, (ushort)NodeField.Visible, 0f),
            new EnginePropertyValueEntry(EngineComponentId.Node, (ushort)NodeField.CastsShadows, 0f),
        ]);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = parent.IsVisible.Should().BeTrue();
        _ = parent.CastsShadows.Should().BeTrue();
        _ = synced[^1].Should().Contain(new EnginePropertyValueEntry(EngineComponentId.Node, (ushort)NodeField.Visible, 1f));

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = parent.IsVisible.Should().BeFalse();
        _ = synced[^1].Should().Contain(new EnginePropertyValueEntry(EngineComponentId.Node, (ushort)NodeField.Visible, 0f));
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenOrthographicCameraChanges_PersistsAndSyncsFraming()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Camera" };
        scene.RootNodes.Add(node);
        var camera = new OrthographicCamera { Name = "Camera", OrthographicSize = 10f, AspectMode = CameraAspectMode.Auto };
        _ = node.AddComponent(camera);
        var context = CreateContext(scene);
        var synced = ConfigureDescriptorSync(fixture, scene);

        var edit = new PropertyEdit();
        edit.Set(SceneDocumentCommandService.OrthographicCamera.OrthographicSize, 4f);
        edit.Set(SceneDocumentCommandService.OrthographicCamera.AspectMode, CameraAspectMode.Fixed);
        var result = await fixture.Sut.EditPropertiesAsync(context, [node.Id], edit, "Edit Camera", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = camera.OrthographicSize.Should().Be(4f);
        _ = camera.AspectMode.Should().Be(CameraAspectMode.Fixed);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = synced.Should().ContainSingle().Which.Should().BeEquivalentTo(
        [
            new EnginePropertyValueEntry(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.OrthographicSize, 4f),
            new EnginePropertyValueEntry(EngineComponentId.OrthographicCamera, (ushort)OrthographicCameraField.AspectMode, (float)CameraAspectMode.Fixed),
        ]);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = camera.OrthographicSize.Should().Be(10f);
        _ = camera.AspectMode.Should().Be(CameraAspectMode.Auto);
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenOrthographicNearReachesFar_RejectsWithoutMutation()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Camera" };
        scene.RootNodes.Add(node);
        var camera = new OrthographicCamera { Name = "Camera", NearPlane = 0.1f, FarPlane = 100f, AspectMode = CameraAspectMode.Auto };
        _ = node.AddComponent(camera);
        var context = CreateContext(scene);
        _ = ConfigureDescriptorSync(fixture, scene);

        var edit = PropertyEdit.SingleEdit(SceneDocumentCommandService.OrthographicCamera.NearPlane, 100f);
        var result = await fixture.Sut.EditPropertiesAsync(context, [node.Id], edit, "Edit Camera", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = camera.NearPlane.Should().Be(0.1f);
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().Contain(published => published.Diagnostics.Any(
            diagnostic => diagnostic.Code == SceneDiagnosticCodes.OrthographicCameraNearFarInvalid));
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenPointLightChanges_SyncsRangeAndFlux()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Lamp" };
        scene.RootNodes.Add(node);
        var light = new PointLightComponent { Name = "Lamp", Range = 10f, LuminousFluxLumens = 800f };
        _ = node.AddComponent(light);
        var context = CreateContext(scene);
        var synced = ConfigureDescriptorSync(fixture, scene);

        var edit = new PropertyEdit();
        edit.Set(new PropertyId<float>(SceneDocumentCommandService.PointLight.RangeDescriptor.Id), 25f);
        edit.Set(new PropertyId<float>(SceneDocumentCommandService.PointLight.LuminousFluxLumensDescriptor.Id), 1600f);
        var result = await fixture.Sut.EditPropertiesAsync(context, [node.Id], edit, "Edit Point Light", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = light.Range.Should().Be(25f);
        _ = light.LuminousFluxLumens.Should().Be(1600f);
        _ = synced.Should().ContainSingle().Which.Should().BeEquivalentTo(
        [
            new EnginePropertyValueEntry(EngineComponentId.PointLight, (ushort)LocalLightField.LuminousFluxLumens, 1600f),
            new EnginePropertyValueEntry(EngineComponentId.PointLight, (ushort)LocalLightField.Range, 25f),
        ]);
    }

    [TestMethod]
    public async Task EditPropertiesAsync_WhenSpotInnerConeExceedsOuter_RejectsWithoutMutation()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Spot" };
        scene.RootNodes.Add(node);
        var light = new SpotLightComponent { Name = "Spot", Range = 10f, InnerConeAngleRadians = 0.3f, OuterConeAngleRadians = 0.5f };
        _ = node.AddComponent(light);
        var context = CreateContext(scene);
        var synced = ConfigureDescriptorSync(fixture, scene);

        var invalid = PropertyEdit.SingleEdit(new PropertyId<float>(SceneDocumentCommandService.SpotLight.InnerConeAngleRadiansDescriptor!.Id), 0.7f);
        var rejected = await fixture.Sut.EditPropertiesAsync(context, [node.Id], invalid, "Edit Spot Light", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = rejected.Succeeded.Should().BeFalse();
        _ = light.InnerConeAngleRadians.Should().Be(0.3f);
        _ = synced.Should().BeEmpty();

        var valid = PropertyEdit.SingleEdit(new PropertyId<float>(SceneDocumentCommandService.SpotLight.OuterConeAngleRadiansDescriptor!.Id), 0.8f);
        var accepted = await fixture.Sut.EditPropertiesAsync(context, [node.Id], valid, "Edit Spot Light", EditSessionToken.OneShot).ConfigureAwait(false);

        _ = accepted.Succeeded.Should().BeTrue();
        _ = light.OuterConeAngleRadians.Should().Be(0.8f);
        _ = synced.Should().ContainSingle().Which.Should().ContainSingle().Which.Should().Be(
            new EnginePropertyValueEntry(EngineComponentId.SpotLight, (ushort)LocalLightField.OuterConeAngleRadians, 0.8f));
    }

    private static List<IReadOnlyList<EnginePropertyValueEntry>> ConfigureDescriptorSync(Fixture fixture, Scene scene)
    {
        var synced = new List<IReadOnlyList<EnginePropertyValueEntry>>();
        var accepted = new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditNodeRendering, AffectedScope.Empty);
        _ = fixture.Sync
            .Setup(sync => sync.UpdatePropertiesAsync(scene, It.IsAny<SceneNode>(), It.IsAny<IReadOnlyList<EnginePropertyValueEntry>>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>()))
            .Callback<Scene, SceneNode, IReadOnlyList<EnginePropertyValueEntry>, SceneSyncRevision, CancellationToken>((_, _, entries, _, _) => synced.Add(entries))
            .ReturnsAsync(accepted);
        _ = fixture.Sync
            .Setup(sync => sync.CompleteTerminalSyncAsync(scene.Id, It.IsAny<Guid>(), It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
            .Returns((Guid _, Guid _, Func<CancellationToken, Task<SyncOutcome>> sync, CancellationToken cancellationToken) => sync(cancellationToken));
        return synced;
    }
}
