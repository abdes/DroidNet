// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Services;

/// <summary>
/// Checks that the projector turns a scene document's changes into engine commands: what reaches the engine, what waits
/// for it, and how its refusals are reported. What the edits do to the scene is the Scenes module's to test.
/// </summary>
public sealed partial class SceneEngineSyncTests
{
    [TestMethod]
    public async Task EditedProperty_ReachesTheEngine()
    {
        using var fixture = new ReplayFixture();
        var (scene, node, documentId) = fixture.RegisterScene(0);
        _ = await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        _ = await fixture.TabOf(documentId).Authoring.ExecuteAsync(Move(node.Id, 7)).ConfigureAwait(false);

        _ = fixture.Requests.Select(static request => request.Command).OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle().Which.Position.Should().Be(new Vector3(7, 0, 0));
    }

    [TestMethod]
    public async Task OfflineEditAndUndo_TheNextSyncSendsTheSceneAndRedoIsLive()
    {
        using var fixture = new ReplayFixture { State = EngineServiceState.Ready };
        var (scene, node, documentId) = fixture.RegisterScene(0);
        var authoring = fixture.TabOf(documentId).Authoring;

        _ = await authoring.ExecuteAsync(Move(node.Id, 7)).ConfigureAwait(false);
        _ = await authoring.UndoAsync().ConfigureAwait(false);
        fixture.State = EngineServiceState.Running;
        _ = (await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.Requests.Select(static request => request.Command).OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle("the snapshot supersedes both the offline edit and its undo").Which.Position.Should().Be(Vector3.Zero);
        _ = fixture.Sync.GetPendingPropertySyncCount(scene.Id).Should().Be(0);
        fixture.Requests.Clear();

        _ = await authoring.RedoAsync().ConfigureAwait(false);

        _ = fixture.Requests.Select(static request => request.Command).OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle().Which.Position.Should().Be(new Vector3(7, 0, 0));
    }

    [TestMethod]
    public async Task EngineRefusal_IsPublishedAndTheSceneKeepsTheChange()
    {
        using var fixture = new ReplayFixture();
        var (scene, _, documentId) = fixture.RegisterScene(0);
        _ = await fixture.Sync.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.25f, 0.5f, 0.75f);
        fixture.RejectedBackground = color;

        var result = await fixture.TabOf(documentId).Authoring
            .ExecuteAsync(new SetSceneProperties(PropertyEdit.SingleEdit(SceneSkyFields.Backdrop, EnvironmentBackdrop.SolidColor).With(SceneSkyFields.SolidColorLightsScene, value: false).With(SceneSkyFields.SolidColor, color), "Edit Background"))
            .ConfigureAwait(false);

        _ = result.Outcome.Should().Be(Oxygen.Editor.Scenes.Authoring.SceneEditOutcome.Applied);
        _ = SceneSkyFields.SolidColorOf(scene.Environment).Should().Be(color);
        _ = fixture.Results.SelectMany(static published => published.Diagnostics)
            .Should().Contain(static diagnostic => diagnostic.Code == LiveSyncDiagnosticCodes.EnvironmentRejected);
    }

    [TestMethod]
    public async Task ClearedMaterialOverride_IsClearedInTheEngine()
    {
        using var fixture = new ReplayFixture();
        var scene = CreateScene();
        var built = new SceneNode(scene) { Name = "Cube" };
        scene.RootNodes.Add(built);
        var target = new MaterialSlotTarget(new Uri("asset:///Engine/Generated/BasicShapes/Cube"), Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = new(target.GeometryUri) };
        geometry.OverrideSlots.Add(new MaterialsSlot { Target = target, Material = new(new Uri("asset:///Content/Materials/Red.omat.json")) });
        built.Components.Add(geometry);
        using var tab = Register(fixture.Sync, scene);
        _ = await fixture.Sync.SyncSceneAsync(tab.Scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        _ = await tab.Authoring.ExecuteAsync(new SetMaterialSlot([built.Id], target, material: null)).ConfigureAwait(false);

        var cleared = fixture.Requests.Select(static request => request.Command).OfType<RuntimeSetMaterialOverride>().Should().ContainSingle().Subject;
        _ = (cleared.SlotId, cleared.MaterialPath).Should().Be((target.SlotId, (string?)null));
    }

    private static SetProperties Move(NodeId node, float x)
        => new(new Dictionary<NodeId, PropertyEdit> { [node] = PropertyEdit.SingleEdit(SceneProperties.Transform.PositionX, x) }, "Move");
}
