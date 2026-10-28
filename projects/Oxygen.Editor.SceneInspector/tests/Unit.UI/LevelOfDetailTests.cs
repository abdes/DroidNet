// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.SceneInspector.Components.Geometry;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// The Level of detail rows (instance-overrides-inspector.md 4; model editor 5.4): each LOD's triangles, the geometry's LOD
/// policy, the nodes' override or the asset's, one boundary per transition between LODs, whole-policy edits in one undo step, refusals with their code, reset
/// to the asset's policy, and mixed policies across nodes that share the geometry.
/// </summary>
[TestClass]
internal sealed class LevelOfDetailTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private static readonly LodPolicy AssetPolicy = new LodPolicy.Distance([5f, 10f, 20f], 0.1f);

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task RowsShowTheAssetPolicyUnmarked() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        _ = lod.IsAvailable.Should().BeTrue();
        _ = lod.Kind.Should().Be(1, "the asset selects its LOD by distance");
        _ = lod.IsDistance.Should().BeTrue();
        _ = lod.Transitions.Select(static row => row.Label).Should().Equal("LOD 0 to 1", "LOD 1 to 2", "LOD 2 to 3");
        _ = lod.Transitions.Select(static row => (float)row.Distance).Should().Equal(5f, 10f, 20f);
        _ = lod.Hysteresis.Should().BeApproximately(0.1, 1e-6);
        _ = lod.Summary.Should().Be("4 LODs, from the asset");
        _ = lod.OverrideState.Should().Be(OverrideState.None, "an asset value is not a change of this scene");
    });

    [TestMethod]
    public Task TrianglesListEachLodFinestFirst() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        _ = await OpenAsync(host).ConfigureAwait(true);
        var geometry = host.PropertyEditors.OfType<GeometryViewModel>().Single();

        _ = geometry.HasLodTriangles.Should().BeTrue();
        _ = geometry.LodTriangles.Should().Be(TrianglesLine(12_480, 6_236, 3_110, 1_552));
    });

    [TestMethod]
    public Task ASingleLodGeometryShowsItsTriangles() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        _ = await OpenAsync(host).ConfigureAwait(true);
        var geometry = host.PropertyEditors.OfType<GeometryViewModel>().Single();

        _ = geometry.LodTriangles.Should().Be(TrianglesLine(12), "the line shows for every geometry, unlike the policy rows");
    });

    [TestMethod]
    public Task ASingleLodGeometryShowsNoRowsUnlessItsPolicyIsOverridden() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        _ = lod.IsAvailable.Should().BeFalse("one LOD always shows, whatever the policy");

        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.LodPolicy, (LodPolicy)new LodPolicy.Fixed(2));
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = lod.IsAvailable.Should().BeTrue("an override shows so it can be reset");
        _ = lod.OverrideState.Should().Be(OverrideState.Full);
    });

    [TestMethod]
    public Task EditingOneThresholdOverridesTheWholePolicyInOneUndoStep() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);
        var steps = fixture.Authoring.Document.History.UndoStack.Count;

        lod.Transitions[1].Distance = 12;
        await lod.PendingEdit.ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var policy).Should().BeTrue();
        _ = policy.Should().Be(new LodPolicy.Distance([5f, 12f, 20f], 0.1f), "the override starts from the asset's values");
        _ = fixture.Authoring.Document.History.UndoStack.Should().HaveCount(steps + 1);
        _ = lod.OverrideState.Should().Be(OverrideState.Full);
        _ = lod.Summary.Should().Be("Overridden; the asset uses Distance");

        await fixture.Authoring.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
    });

    [TestMethod]
    public Task AThresholdOutOfOrderIsRefusedWithItsCodeAndNeverReordered() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        lod.Transitions[1].Distance = 2;
        await lod.PendingEdit.ConfigureAwait(true);

        _ = lod.ErrorCode.Should().Be(SceneEditCodes.PropertyOutOfRange);
        _ = lod.HasError.Should().BeTrue();
        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
        _ = lod.Transitions.Select(static row => (float)row.Distance).Should().Equal(5f, 10f, 20f);
    });

    [TestMethod]
    public Task FixedPinsALodWithinTheGeometryAndResetReturnsToTheAssetPolicy() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        lod.Kind = 0;
        await lod.PendingEdit.ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var pinned).Should().BeTrue();
        _ = pinned.Should().Be(new LodPolicy.Fixed(0));
        _ = lod.IsFixed.Should().BeTrue();
        _ = lod.FixedRange.Should().Be("of 0 to 3");
        _ = lod.ResetToolTip.Should().Be("Reset to the asset's policy (Distance)");

        lod.FixedLod = 2;
        await lod.PendingEdit.ConfigureAwait(true);
        lod.FixedLod = 4;

        _ = lod.HasError.Should().BeTrue("the geometry has no LOD 4");
        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var second).Should().BeTrue();
        _ = second.Should().Be(new LodPolicy.Fixed(2));

        await lod.ResetCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.IsEmpty.Should().BeTrue();
        _ = lod.IsDistance.Should().BeTrue("the asset's policy applies again");
        _ = lod.OverrideState.Should().Be(OverrideState.None);
    });

    [TestMethod]
    public Task ScreenSpaceErrorStartsWithTheImportDefault() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        lod.Kind = 2;
        await lod.PendingEdit.ConfigureAwait(true);
        lod.Transitions[2].Coarser = 40;
        await lod.PendingEdit.ConfigureAwait(true);

        _ = fixture.Geometry.Overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var policy).Should().BeTrue();
        _ = policy.Should().Be(new LodPolicy.ScreenSpaceError([256f, 128f, 64f], [204.8f, 102.4f, 40f]));
        _ = lod.Transitions.Select(static row => (float)row.Finer).Should().Equal(256f, 128f, 64f);
    });

    [TestMethod]
    public Task NodesWithDifferentPoliciesShowMixedAndAKindSetsItOnAll() => EnqueueAsync(async () =>
    {
        using var fixture = new GeometryFixture();
        PublishFourLods(fixture);
        fixture.Geometry.Overrides = fixture.Geometry.Overrides.With(SceneSchema.Geometry.LodPolicy, (LodPolicy)new LodPolicy.Fixed(1));
        var second = new SceneNode(fixture.Authoring.Scene, "Second");
        var secondGeometry = new GeometryComponent { Name = "Geometry", Geometry = fixture.Geometry.Geometry };
        _ = second.AddComponent(secondGeometry);
        fixture.Authoring.Scene.RootNodes.Add(second);
        using var host = fixture.Authoring.CreateInspectorHost("Geometry", assetProvider: fixture.Assets.Object);
        fixture.Authoring.Select(fixture.Authoring.Node, second);
        await WaitForRenderAsync().ConfigureAwait(true);
        var lod = await OpenAsync(host).ConfigureAwait(true);

        _ = lod.Kind.Should().Be(-1);
        _ = lod.OverrideState.Should().Be(OverrideState.Partial);
        _ = lod.Summary.Should().Be("Mixed: the selected nodes use different policies");
        _ = lod.ResetToolTip.Should().Be("Reset to the asset's policy (Distance), on the 1 of 2 nodes that change it");
        _ = lod.Transitions.Should().BeEmpty("mixed policies show no boundaries");

        lod.Kind = 1;
        await lod.PendingEdit.ConfigureAwait(true);

        _ = lod.OverrideState.Should().Be(OverrideState.Full, "choosing a kind writes it on every selected node");
        _ = secondGeometry.Overrides.TryGet(SceneSchema.Geometry.LodPolicy, out var policy).Should().BeTrue();
        _ = policy.Should().Be(AssetPolicy, "a kind the asset uses starts from the asset's values");
    });

    private static string TrianglesLine(params ulong[] counts)
        => string.Join(" · ", counts.Select(static count => count.ToString("N0", CultureInfo.CurrentCulture)));

    private static async Task<LodPolicyEditorModel> OpenAsync(SceneInspectorViewModel host)
    {
        var model = host.PropertyEditors.OfType<GeometryViewModel>().Single();
        await model.RefreshSurfacesAsync().ConfigureAwait(true);
        return model.LevelOfDetail;
    }

    // One surface bound in each of four LODs, each with half the previous LOD's triangles or so, with a Distance
    // policy.
    private static void PublishFourLods(GeometryFixture fixture)
    {
        var surface = SceneAuthoringFixture.TargetFor(fixture.Geometry.Geometry);
        fixture.Authoring.Surfaces.Publish(new GeometrySurfaceMetadata(
            fixture.Geometry.Geometry,
            Guid.Parse("20000000-0000-0000-0000-000000000001"),
            surface.LayoutRevision,
            [new(surface.SurfaceId, "Body", Visible: true, CastsShadows: true, [new(0, 0, Guid.Empty, TriangleCount: 12_480), new(1, 0, Guid.Empty, TriangleCount: 6_236), new(2, 0, Guid.Empty, TriangleCount: 3_110), new(3, 0, Guid.Empty, TriangleCount: 1_552)])],
            AssetPolicy));
    }
}
