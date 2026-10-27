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
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>
/// Checks that the projector turns a scene document's changes into engine commands: what reaches the engine, what waits
/// for it, and how its refusals are reported. What the edits do to the scene is the Scenes module's to test.
/// </summary>
public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task EditedProperty_ReachesTheEngine()
    {
        using var fixture = new PreviewFixture();
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        _ = await PreviewFixture.AuthoringOf(document).ExecuteAsync(Move(node.Id, 7)).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle().Which.Position.Should().Be(new Vector3(7, 0, 0));
    }

    [TestMethod]
    public async Task OfflineEditAndUndo_TheNextSyncSendsTheSceneAndRedoIsLive()
    {
        using var fixture = new PreviewFixture(EngineServiceState.Ready);
        var (scene, node, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var authoring = PreviewFixture.AuthoringOf(document);

        _ = await authoring.ExecuteAsync(Move(node.Id, 7)).ConfigureAwait(false);
        _ = await authoring.UndoAsync().ConfigureAwait(false);
        fixture.State = EngineServiceState.Running;
        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle("the snapshot supersedes both the offline edit and its undo").Which.Position.Should().Be(Vector3.Zero);
        _ = fixture.Preview.PendingFieldCount(document.Id).Should().Be(0);
        fixture.Requests.Clear();

        _ = await authoring.RedoAsync().ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeSetLocalTransform>()
            .Should().ContainSingle().Which.Position.Should().Be(new Vector3(7, 0, 0));
    }

    [TestMethod]
    public async Task EngineRefusal_IsPublishedAndTheSceneKeepsTheChange()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, document) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        var color = new Vector3(0.25f, 0.5f, 0.75f);
        fixture.RejectedBackground = color;

        var result = await PreviewFixture.AuthoringOf(document)
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
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var cube = new SceneNode(built, "Cube");
        built.RootNodes.Add(cube);
        var target = new SurfaceTarget(EngineAssets.BasicShape("Cube"), Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = target.Geometry };
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, target.Part, target.LayoutRevision, AssetPath.Parse("/Content/Materials/Red.omat"));
        cube.Components.Add(geometry);
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        _ = await PreviewFixture.AuthoringOf(document).ExecuteAsync(new SetOverrides([cube.Id], [new OverrideWrite(SceneSchema.Geometry.Surface.Material.Id, target.Part, target.LayoutRevision, Value: null)], "Edit Surface Material")).ConfigureAwait(false);

        var cleared = fixture.World.Commands.OfType<RuntimeSetOverrides>().Should().ContainSingle().Subject;
        _ = cleared.Part.Should().Be(new RuntimeOverridePart(target.SurfaceId, target.LayoutRevision));
        _ = cleared.Overrides.Should().BeEmpty();
    }

    [TestMethod]
    public async Task EditedLodPolicy_ReachesTheEngineAndItsResetClearsIt()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Cube");
        built.RootNodes.Add(node);
        node.Components.Add(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();
        var authoring = PreviewFixture.AuthoringOf(document);
        var policy = new LodPolicy.Fixed(1);

        _ = await authoring.ExecuteAsync(new SetOverrides([node.Id], [new OverrideWrite(SceneSchema.Geometry.LodPolicy.Id, Part: null, LayoutRevision: null, policy)], "Edit Level of Detail")).ConfigureAwait(false);

        var set = fixture.World.Commands.OfType<RuntimeSetOverrides>().Should().ContainSingle().Subject;
        _ = set.Part.Should().BeNull("the policy belongs to the geometry component, not to a surface");
        _ = set.Overrides.Should().Equal(new RuntimeOverride(RuntimeOverrideId.GeometryLodPolicy, new RuntimeOverrideValue.LodPolicyValue(policy)));
        fixture.Requests.Clear();

        _ = await authoring.ExecuteAsync(new SetOverrides([node.Id], [new OverrideWrite(SceneSchema.Geometry.LodPolicy.Id, Part: null, LayoutRevision: null, Value: null)], "Reset Level of Detail")).ConfigureAwait(false);

        var cleared = fixture.World.Commands.OfType<RuntimeSetOverrides>().Should().ContainSingle().Subject;
        _ = cleared.Part.Should().BeNull();
        _ = cleared.Overrides.Should().BeEmpty("the asset's policy applies again");
    }

    [TestMethod]
    public async Task ImportedGeometry_IsAttachedInTheEngine()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Crate");
        built.RootNodes.Add(node);
        node.Components.Add(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);
        _ = await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();
        fixture.Results.Clear();
        var imported = AssetPath.Parse("/Imported/Crate/Geometry/Crate.ogeo");

        _ = await PreviewFixture.AuthoringOf(document).ExecuteAsync(new SetGeometry([node.Id], imported)).ConfigureAwait(false);

        _ = fixture.World.Commands.OfType<RuntimeSetGeometry>().Should().ContainSingle(command => command.NodeId == node.Id);
        _ = fixture.Results.SelectMany(static result => result.Diagnostics)
            .Should().NotContain(static diagnostic => diagnostic.Code == LiveSyncDiagnosticCodes.GeometryUnresolvedAtRuntime);
    }

    private static SetProperties Move(NodeId node, float x)
        => new(new Dictionary<NodeId, PropertyEdit> { [node] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, x) }, "Move");
}
