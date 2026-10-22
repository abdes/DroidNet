// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Properties;

/// <summary>
/// What is specific to <see cref="SetOverrides"/>: an override is stored only for a property a source supplies, on a part
/// of its kind, with a value of its type that passes its rule, and only on a node still using the asset the part was
/// chosen from; a reset removes it. Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetOverridesTests
{
    private static readonly AssetPath BlueMaterial = AssetPath.Parse("/Content/Blue.omat");
    private static readonly PropertyId Material = SceneSchema.Geometry.Surface.Material.Id;

    private static SurfaceTarget CubeTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSurface, SampleScene.CubeLayoutRevision);

    [TestMethod]
    public void EveryOverridableProperty_HasARule()
        => _ = OverrideRules.Properties.Should().BeEquivalentTo(SceneSchema.Overridable.Keys);

    [TestMethod]
    public async Task Value_OverridesTheSurfaceAndRaisesOverridesChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(Write(BlueMaterial)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = MaterialOf(fixture, SampleScene.Mesh, CubeTarget.Part).Should().Be(BlueMaterial);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.OverridesChanged(SampleScene.Mesh, [Material]));
    }

    [TestMethod]
    public async Task Null_ResetsToTheSourceAndUndoRestoresTheOverride()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, Write(material: null)).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        _ = Geometry(fixture, SampleScene.Mesh).Overrides.IsEmpty.Should().BeTrue("a reset never writes the source's value");
        _ = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        _ = MaterialOf(fixture, SampleScene.Mesh, CubeTarget.Part).Should().Be(SampleScene.RedMaterial);
    }

    [TestMethod]
    public async Task NullOnASurfaceWithoutOverride_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Write(material: null, CubeTarget with { SurfaceId = Guid.NewGuid() })).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NewerRevision_BecomesThePartsAndUndoRestoresTheOlder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var newer = CubeTarget with { LayoutRevision = new string('d', 64) };

        _ = await SceneAssert.RoundTripAsync(fixture, Write(BlueMaterial, newer)).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        _ = Geometry(fixture, SampleScene.Mesh).Overrides.Find(Material, CubeTarget.Part)
            .Should().Be(new SourceOverride(Material, CubeTarget.Part, newer.LayoutRevision, BlueMaterial));
    }

    [TestMethod]
    public async Task GeometryChangedSinceTheSurfaceWasChosen_IsStale()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Write(BlueMaterial)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PartStale, SampleScene.Mesh, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task TwoNodesOneOnOtherGeometry_ChangesNeither()
    {
        var sphere = NodeId.New();
        var data = SampleScene.Builder()
            .Node(sphere, "Sphere", components: [new GeometryComponentData { Id = ComponentId.New(), Name = "Geometry", GeometryUri = SampleScene.SphereGeometry }])
            .Build();
        using var fixture = await AuthoringFixture.CreateAsync(data).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Write(BlueMaterial, nodes: [SampleScene.Mesh, sphere])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PartStale, sphere, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NotAMaterialAsset_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Write(SampleScene.CubeGeometry)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid, SampleScene.Mesh, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task APropertyWithoutASource_IsUnknown()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var write = new OverrideWrite(SceneSchema.Geometry.GeometryUri.Id, Part: null, LayoutRevision: null, SampleScene.SphereGeometry);

        var result = await fixture.Authoring.ExecuteAsync(new SetOverrides([SampleScene.Mesh], [write], "Edit")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyUnknown, SampleScene.Mesh, SceneSchema.Geometry.GeometryUri.Id);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow(null, DisplayName = "Without a part")]
    [DataRow("section", DisplayName = "On another part kind")]
    public async Task APartPropertyNotOnItsPart_IsInvalid(string? partKind)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        PartId? part = partKind is null ? null : new PartId(partKind, SampleScene.CubeSurface);
        var write = new OverrideWrite(Material, part, SampleScene.CubeLayoutRevision, BlueMaterial);

        var result = await fixture.Authoring.ExecuteAsync(new SetOverrides([SampleScene.Mesh], [write], "Edit")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.OverrideInvalid, SampleScene.Mesh, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task AValueWithoutALayoutRevision_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var write = new OverrideWrite(Material, CubeTarget.Part, LayoutRevision: null, BlueMaterial);

        var result = await fixture.Authoring.ExecuteAsync(new SetOverrides([SampleScene.Mesh], [write], "Edit")).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.OverrideInvalid, SampleScene.Mesh, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NodeWithoutTheComponent_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Write(BlueMaterial, nodes: [SampleScene.B])).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    private static SetOverrides Write(AssetPath? material, SurfaceTarget? target = null, IReadOnlyCollection<NodeId>? nodes = null)
    {
        var surface = target ?? CubeTarget;
        return new SetOverrides(
            nodes ?? [SampleScene.Mesh],
            [new OverrideWrite(Material, surface.Part, surface.LayoutRevision, material) { PartOwner = surface.Geometry }],
            "Edit Surface Material");
    }

    private static GeometryComponent Geometry(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<GeometryComponent>().Single();

    private static AssetPath? MaterialOf(AuthoringFixture fixture, NodeId node, PartId part)
        => Geometry(fixture, node).Overrides.TryGet(SceneSchema.Geometry.Surface.Material, part, out var material) ? material : null;
}
