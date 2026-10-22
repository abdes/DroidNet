// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Edits.Assets;

/// <summary>
/// What is specific to <see cref="SurfaceMaterialEdits"/> (scene-edits.md 4, "Surface inventories"): an inventory is
/// read before the edit, which then checks the node's geometry again when it runs.
/// </summary>
[TestClass]
public sealed class SurfaceInventoryTests
{
    private static readonly AssetPath BlueMaterial = AssetPath.Parse("/Content/Blue.omat");

    private readonly TestProject project = TestProject.Create(new MockFileSystem());

    public TestContext TestContext { get; set; }

    private static SurfaceTarget CubeTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSurface, SampleScene.CubeLayoutRevision);

    [TestMethod]
    public async Task Read_BuildsTheEditWithSurfaceIdsAndRevision()
    {
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSurface);

        var built = await SurfaceMaterialEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Error.Should().BeNull();
        var edit = built.Edit.Should().BeOfType<SetSurfaceMaterial>().Subject;
        _ = edit.Assignments.Keys.Should().Equal(SampleScene.Mesh);
        _ = edit.Assignments[SampleScene.Mesh].Should().Be(new SurfaceMaterialAssignment(CubeTarget, BlueMaterial));
    }

    [TestMethod]
    public async Task InventoryUnavailable_ReportsIt()
    {
        using var provider = new FakeSurfaceProvider();

        var built = await SurfaceMaterialEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Edit.Should().BeNull();
        _ = built.Error.Should().NotBeNull();
        _ = built.Error!.IsError.Should().BeTrue();
        _ = built.Error.Code.Should().Be(SceneEditCodes.SurfaceInventoryUnavailable);
    }

    [TestMethod]
    public async Task GeometryChangedDuringRead_EditIsStale()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSurface);
        using var gate = provider.HoldReads();
        var reading = SurfaceMaterialEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken);
        await gate.ReadPending.ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);
        gate.Release();
        var built = await reading.ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.SurfaceStale, SampleScene.Mesh);
        SceneAssert.Unchanged(fixture, before);
    }
}
