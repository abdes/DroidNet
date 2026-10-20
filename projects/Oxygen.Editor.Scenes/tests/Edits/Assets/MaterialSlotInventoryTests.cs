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
/// What is specific to <see cref="MaterialSlotEdits"/> (scene-edits.md 4, "Material slot inventories"): an inventory is
/// read before the edit, which then checks the node's geometry again when it runs.
/// </summary>
[TestClass]
public sealed class MaterialSlotInventoryTests
{
    private static readonly AssetPath BlueMaterial = AssetPath.Parse("/Content/Blue.omat");

    private readonly TestProject project = TestProject.Create(new MockFileSystem());

    public TestContext TestContext { get; set; }

    private static MaterialSlotTarget CubeTarget => new(SampleScene.CubeGeometry, SampleScene.CubeSlot, SampleScene.CubeLayoutRevision);

    [TestMethod]
    public async Task Read_BuildsTheEditWithSlotIdsAndRevision()
    {
        using var provider = new FakeMaterialSlotProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSlot);

        var built = await MaterialSlotEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Error.Should().BeNull();
        var edit = built.Edit.Should().BeOfType<SetMaterialSlot>().Subject;
        _ = edit.Assignments.Keys.Should().Equal(SampleScene.Mesh);
        _ = edit.Assignments[SampleScene.Mesh].Should().Be(new MaterialSlotAssignment(CubeTarget, BlueMaterial));
    }

    [TestMethod]
    public async Task InventoryUnavailable_ReportsIt()
    {
        using var provider = new FakeMaterialSlotProvider();

        var built = await MaterialSlotEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = built.Edit.Should().BeNull();
        _ = built.Error.Should().NotBeNull();
        _ = built.Error!.IsError.Should().BeTrue();
        _ = built.Error.Code.Should().Be(SceneEditCodes.MaterialSlotInventoryUnavailable);
    }

    [TestMethod]
    public async Task GeometryChangedDuringRead_EditIsStale()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeMaterialSlotProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, SampleScene.CubeSlot);
        using var gate = provider.HoldReads();
        var reading = MaterialSlotEdits.SetAsync(provider, this.project.Context, [SampleScene.Mesh], CubeTarget, BlueMaterial, this.TestContext.CancellationToken);
        await gate.ReadPending.ConfigureAwait(false);
        _ = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);
        gate.Release();
        var built = await reading.ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.MaterialSlotStale, SampleScene.Mesh);
        SceneAssert.Unchanged(fixture, before);
    }
}
