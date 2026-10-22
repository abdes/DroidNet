// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Layout;

/// <summary>
/// What is specific to <see cref="RemoveFolder"/> (scene-edits.md 4): removing a folder regroups, it never removes
/// nodes. Targets, locks, undo and the change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveFolderTests
{
    // The sample's root after F: Cam, Ortho, Sun, Moon, Lamp, Spot, Fog, Mesh, Script.
    private static NodeId[] RootAfterF => [SampleScene.Cam, SampleScene.Ortho, SampleScene.Sun, SampleScene.Moon, SampleScene.Lamp, SampleScene.Spot, SampleScene.Fog, SampleScene.Mesh, SampleScene.Script];

    [TestMethod]
    public async Task PromoteChildren_PutsThemAtTheFolderPositionInOrder()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.F, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        LayoutEntryId[] expected = [LayoutEntryId.Of(SampleScene.A), LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G), .. RootAfterF.Select(static node => LayoutEntryId.Of(node))];
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.AtRoot(0)).Should().Equal(expected);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.F)).Should().BeNull();
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.LayoutChanged());
    }

    [TestMethod]
    public async Task PromoteChildren_KeepsParentsAndWorldTransforms()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var worldBefore = SceneTransformMath.WorldMatrix(fixture.Node(SampleScene.A2));

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.AF, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.UnderNode(SampleScene.A, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.A1), LayoutEntryId.Of(SampleScene.A2));
        _ = fixture.Node(SampleScene.A2).Hierarchy.Parent!.Id.Should().Be(SampleScene.A);
        _ = SceneTransformMath.WorldMatrix(fixture.Node(SampleScene.A2)).Should().Be(worldBefore);
    }

    [TestMethod]
    public async Task PromoteChildren_NestedFolderIsPromotedWhole()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.F, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.G)).Should().Be(LayoutPosition.AtRoot(2));
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.G).Should().NotBeNull();
    }

    [TestMethod]
    public async Task Undo_RestoresTheFolderPositionChildrenAndExpandedState()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, new RemoveFolder(SampleScene.F, PromoteChildren: true)).ConfigureAwait(false);

        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.F)).Should().Be(LayoutPosition.AtRoot(1));
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.F)!.IsExpanded.Should().BeTrue();
        _ = LayoutQuery.Folder(fixture.Layout, SampleScene.G)!.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    public async Task FolderContainingLockedNode_IsRemoved()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.B);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.F, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.B)).Should().Be(LayoutPosition.AtRoot(1));
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.F)).Should().BeNull();
    }

    [TestMethod]
    public async Task InLockedNodeScope_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        fixture.Interaction!.Lock(SampleScene.A);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.AF, PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.A);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task MissingFolder_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(FolderId.New(), PromoteChildren: true)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderMissing);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task WithoutPromotion_EmptyFolder_IsRemoved()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.G, PromoteChildren: false)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.PositionOf(fixture.Layout, LayoutEntryId.Of(SampleScene.G)).Should().BeNull();
        _ = LayoutScopes.EntriesOf(fixture.Layout, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.B));
    }

    [TestMethod]
    public async Task WithoutPromotion_NonEmptyFolder_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveFolder(SampleScene.F, PromoteChildren: false)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.FolderNotEmpty);
        SceneAssert.Unchanged(fixture, before);
    }
}
