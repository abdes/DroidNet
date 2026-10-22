// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Edits.Assets;

/// <summary>
/// What is specific to <see cref="SurfaceEdits"/> (scene-edits.md 4, "Surface inventories"): an inventory is read before
/// the edit, which names the geometry the surface was chosen from, so the edit checks the node's geometry again when it
/// runs; overrides of surfaces the inventory lacks are reset together.
/// </summary>
[TestClass]
public sealed class SurfaceEditsTests
{
    private static readonly AssetPath BlueMaterial = AssetPath.Parse("/Content/Blue.omat");
    private static readonly PropertyId Material = SceneSchema.Geometry.Surface.Material.Id;

    private readonly TestProject project = TestProject.Create(new MockFileSystem());

    public TestContext TestContext { get; set; }

    private static SurfaceTarget CubeTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSurface, SampleScene.CubeLayoutRevision);

    [TestMethod]
    public async Task Read_BuildsTheWriteWithTheSurfaceRevisionAndGeometry()
    {
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSurface);

        var built = await this.SetAsync(provider, CubeTarget).ConfigureAwait(false);

        _ = built.Error.Should().BeNull();
        var edit = built.Edit.Should().BeOfType<SetOverrides>().Subject;
        _ = edit.Writes.Keys.Should().Equal(SampleScene.Mesh);
        _ = edit.Writes[SampleScene.Mesh].Should().Equal(
            new OverrideWrite(Material, CubeTarget.Part, SampleScene.CubeLayoutRevision, BlueMaterial) { PartOwner = SampleScene.CubeGeometry });
    }

    [TestMethod]
    public async Task InventoryUnavailable_ReportsIt()
    {
        using var provider = new FakeSurfaceProvider();

        var built = await this.SetAsync(provider, CubeTarget).ConfigureAwait(false);

        _ = built.Edit.Should().BeNull();
        _ = built.Error.Should().NotBeNull();
        _ = built.Error!.IsError.Should().BeTrue();
        _ = built.Error.Code.Should().Be(SceneEditCodes.SurfaceInventoryUnavailable);
    }

    [TestMethod]
    public async Task SurfaceNotInTheInventory_IsStale()
    {
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, Guid.NewGuid());

        var built = await this.SetAsync(provider, CubeTarget).ConfigureAwait(false);

        _ = built.Edit.Should().BeNull();
        _ = built.Error!.Code.Should().Be(SceneEditCodes.SurfaceStale);
    }

    [TestMethod]
    public async Task GeometryChangedDuringRead_EditIsStale()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSurface);
        using var gate = provider.HoldReads();
        var reading = this.SetAsync(provider, CubeTarget);
        await gate.ReadPending.ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);
        gate.Release();
        var built = await reading.ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PartStale, SampleScene.Mesh, Material);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnresolvedSurfaces_AreResetAndUndoRestoresThem()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, Guid.NewGuid());
        var built = await SurfaceEdits.ClearUnresolvedAsync(provider, fixture.Project.Context, fixture.Scene, [SampleScene.Mesh], this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.Mesh).Components.OfType<GeometryComponent>().Single().Overrides.IsEmpty.Should().BeTrue();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.OverridesChanged(SampleScene.Mesh, [Material]));
        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(false)).Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    public async Task EveryOverrideResolves_BuildsNoEdit()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSurface);

        var built = await SurfaceEdits.ClearUnresolvedAsync(provider, fixture.Project.Context, fixture.Scene, [SampleScene.Mesh], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Should().Be(new SurfaceEdit(Edit: null, Error: null));
    }

    [TestMethod]
    public async Task InventoryUnreadable_KeepsTheOverrides()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();

        var built = await SurfaceEdits.ClearUnresolvedAsync(provider, fixture.Project.Context, fixture.Scene, [SampleScene.Mesh], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Edit.Should().BeNull("an override stays until an inventory proves it unresolved");
    }

    private Task<SurfaceEdit> SetAsync(FakeSurfaceProvider provider, SurfaceTarget target)
        => SurfaceEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], target, Material, BlueMaterial, "Edit Surface Material", this.TestContext.CancellationToken);
}
