// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Assets;

/// <summary>
/// What is specific to <see cref="RemoveSurfaceMaterials"/> (scene-edits.md 4): the overrides no surface resolves are
/// removed, and only by this edit. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveSurfaceMaterialsTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task UnresolvedSurfaces_AreRemovedAndUndoRestoresThem()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        using var provider = new FakeSurfaceProvider();
        provider.Publish(SampleScene.CubeGeometry, SampleScene.CubeLayoutRevision, Guid.NewGuid());
        var built = await SurfaceMaterialEdits.ClearUnresolvedAsync(provider, fixture.Project.Context, fixture.Scene, [SampleScene.Mesh], this.TestContext.CancellationToken).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(built.Edit!).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.Mesh).Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().Should().BeEmpty();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.SurfaceMaterialsChanged(SampleScene.Mesh));
        _ = (await fixture.Authoring.UndoAsync().ConfigureAwait(false)).Outcome.Should().Be(SceneEditOutcome.Applied);
        _ = fixture.Snapshot().Document.Should().Be(before.Document, "undo restores the override at its position");
    }

    [TestMethod]
    public async Task NothingUnresolved_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var absent = new SurfaceTarget(SampleScene.CubeGeometry, Guid.NewGuid(), SampleScene.CubeLayoutRevision);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveSurfaceMaterials(new Dictionary<NodeId, IReadOnlyList<SurfaceTarget>> { [SampleScene.Mesh] = [absent] })).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }
}
