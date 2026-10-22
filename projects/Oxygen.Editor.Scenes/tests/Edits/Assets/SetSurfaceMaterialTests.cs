// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Assets;

/// <summary>
/// What is specific to <see cref="SetSurfaceMaterial"/> (scene-edits.md 3.11 and 4): the surface is matched by geometry and
/// surface id, and every node must still use the surface's geometry and inventory revision. Targets, locks, undo and the
/// change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetSurfaceMaterialTests
{
    private static readonly AssetPath BlueMaterial = AssetPath.Parse("/Content/Blue.omat");

    private static SurfaceTarget CubeTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSurface, SampleScene.CubeLayoutRevision);

    [TestMethod]
    public async Task Material_AssignsTheSurfaceAndRaisesSurfaceMaterialsChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh], CubeTarget, BlueMaterial)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = OverrideOf(fixture, SampleScene.Mesh, SampleScene.CubeSurface)!.Material.Should().Be(BlueMaterial);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.SurfaceMaterialsChanged(SampleScene.Mesh));
    }

    [TestMethod]
    public async Task Null_RemovesTheOverride()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh], CubeTarget, material: null)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = OverrideOf(fixture, SampleScene.Mesh, SampleScene.CubeSurface).Should().BeNull();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.SurfaceMaterialsChanged(SampleScene.Mesh));
    }

    [TestMethod]
    public async Task NullOnClearSurface_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clearSurface = CubeTarget with { SurfaceId = Guid.NewGuid() };
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh], clearSurface, material: null)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task GeometryChangedSinceRead_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var read = CubeTarget;
        _ = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh], read, BlueMaterial)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.SurfaceStale, SampleScene.Mesh);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NewerRevision_ReplacesTheOverrideAndUndoRestoresIt()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var newerRevision = CubeTarget with { LayoutRevision = new string('d', 64) };

        _ = await SceneAssert.RoundTripAsync(fixture, new SetSurfaceMaterial([SampleScene.Mesh], newerRevision, BlueMaterial)).ConfigureAwait(false);
        _ = await fixture.Authoring.RedoAsync().ConfigureAwait(false);

        var assigned = OverrideOf(fixture, SampleScene.Mesh, SampleScene.CubeSurface)!;
        _ = assigned.Material.Should().Be(BlueMaterial);
        _ = assigned.Target.LayoutRevision.Should().Be(newerRevision.LayoutRevision);
    }

    [TestMethod]
    public async Task NotAMaterialAsset_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh], CubeTarget, SampleScene.CubeGeometry)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid, SampleScene.Mesh);
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

        var result = await fixture.Authoring.ExecuteAsync(new SetSurfaceMaterial([SampleScene.Mesh, sphere], CubeTarget, BlueMaterial)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.SurfaceStale, sphere);
        SceneAssert.Unchanged(fixture, before);
    }

    private static MaterialsSlot? OverrideOf(AuthoringFixture fixture, NodeId node, Guid surface)
        => fixture.Node(node).Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().SingleOrDefault(candidate => candidate.Target.SurfaceId == surface);
}
