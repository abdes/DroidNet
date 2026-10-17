// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Nodes;

/// <summary>
/// <see cref="SceneClipboard"/> (scene-edits.md 4): a copied selection as data, and the batch that pastes it.
/// </summary>
[TestClass]
public sealed class SceneClipboardTests
{
    public static IEnumerable<TestDataRow<PasteCase>> OwnDescendants =>
    [
        new(new PasteCase([SampleScene.A], [], new PasteTarget(Parent: SampleScene.A1))) { DisplayName = "NodeAIntoA1" },
        new(new PasteCase([], [SampleScene.F], new PasteTarget(Folder: SampleScene.G))) { DisplayName = "FolderFIntoG" },
    ];

    private static NodeId Singular { get; } = NodeId.FromValue(SampleScene.Tagged(1, "D3"));

    private static NodeId UnderSingular { get; } = NodeId.FromValue(SampleScene.Tagged(1, "D4"));

    private static NodeId OtherRoot { get; } = NodeId.FromValue(SampleScene.Tagged(1, "D5"));

    [TestMethod]
    public async Task Capture_SnapshotsRootsAndLayoutEntries()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var clipboard = SceneClipboard.Capture(fixture.Scene, [SampleScene.A], [SampleScene.F]);

        _ = clipboard.Should().NotBeNull();
        var entries = clipboard!.Entries;
        _ = entries.Should().HaveCount(2);
        _ = entries[0].NodeId.Should().Be(SampleScene.A);
        _ = entries[0].Children!.Select(IdOf).Should().Equal(LayoutEntryId.Of(SampleScene.A1), LayoutEntryId.Of(SampleScene.AF));
        _ = entries[1].FolderId.Should().Be(SampleScene.F);
        _ = entries[1].Children!.Select(IdOf).Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));

        // The top-level nodes are A and B, which sits in the folder; the world snapshot holds the same nodes.
        _ = clipboard.Nodes.Select(static node => node.Id).Should().Equal(SampleScene.A, SampleScene.B);
        _ = clipboard.Nodes[0].Children!.Select(static node => node.Id).Should().Equal(SampleScene.A1, SampleScene.A2);
        _ = clipboard.WorldNodes!.Select(static node => node.Id).Should().Equal(SampleScene.A, SampleScene.B);
    }

    [TestMethod]
    public async Task Capture_Nothing_ReturnsNull()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var clipboard = SceneClipboard.Capture(fixture.Scene, [NodeId.New()], [FolderId.New()]);

        _ = clipboard.Should().BeNull();
    }

    [TestMethod]
    public async Task Capture_UnrepresentableWorldTransform_HasNoWorldNodes()
    {
        var scene = SampleScene.Builder()
            .Node(Singular, "Singular", transform: SampleScene.Transform(scale: new Vector3(0, 1, 1)))
            .Node(UnderSingular, "UnderSingular", parent: Singular)
            .Build();
        using var fixture = await AuthoringFixture.CreateAsync(scene).ConfigureAwait(false);

        var clipboard = SceneClipboard.Capture(fixture.Scene, [UnderSingular], []);

        _ = clipboard.Should().NotBeNull();
        _ = clipboard!.WorldNodes.Should().BeNull();
        _ = clipboard.Nodes.Select(static node => node.Id).Should().Equal(UnderSingular);
    }

    [TestMethod]
    public async Task Capture_SourceEditedAfterwards_PastesTheCapture()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.B], []);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false));
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(), cut: false);

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var pasted = fixture.Scene.AllNodes.Where(static node => string.Equals(node.Name, "B", StringComparison.Ordinal)).Should().ContainSingle().Which;
        _ = pasted.Id.Should().NotBe(SampleScene.B);
        _ = fixture.Node(SampleScene.B).Name.Should().Be("Box");
    }

    [TestMethod]
    public async Task PasteCopy_GivesFoldersNewIdsAndKeepsNamesAndExpandedState()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [], [SampleScene.F]);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(), cut: false);
        var rootBefore = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0));

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var added = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0)).Except(rootBefore).Should().ContainSingle().Which;
        var copy = added.Folder!.Value;
        _ = copy.Should().NotBe(SampleScene.F);
        _ = LayoutQuery.Folder(fixture.Scene, copy)!.Name.Should().Be("F");

        // The explorer reads a missing flag as collapsed, so a copy that lost the flag would show collapsed.
        _ = (LayoutQuery.Folder(fixture.Scene, copy)!.IsExpanded ?? false).Should().BeTrue();

        var inside = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.InFolder(copy, 0));
        _ = inside.Should().HaveCount(2);
        _ = inside[0].Node.Should().NotBeNull().And.NotBe(SampleScene.B);
        var nested = inside[1].Folder!.Value;
        _ = nested.Should().NotBe(SampleScene.G);
        _ = LayoutQuery.Folder(fixture.Scene, nested)!.Name.Should().Be("G");
        _ = (LayoutQuery.Folder(fixture.Scene, nested)!.IsExpanded ?? false).Should().BeFalse();
    }

    [TestMethod]
    [DataRow(true, DisplayName = "PreserveWorld")]
    [DataRow(false, DisplayName = "KeepLocal")]
    public async Task PasteCopy_TransformMode(bool preserveWorld)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.A2));
        var worldBefore = NodePose.WorldOf(fixture.Node(SampleScene.A2));
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.A2));
        var clipboard = Capture(fixture, [SampleScene.A2], []);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(Parent: SampleScene.A1), cut: false, preserveWorld);
        var childrenBefore = fixture.Node(SampleScene.A1).Children.Select(static node => node.Id).ToList();

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var pasted = fixture.Node(SampleScene.A1).Children.Single(node => !childrenBefore.Contains(node.Id));
        if (preserveWorld)
        {
            NodePose.AssertSameWorld(NodePose.WorldOf(pasted), worldBefore);
        }
        else
        {
            _ = NodePose.LocalOf(pasted).Should().Be(localBefore);
        }
    }

    [TestMethod]
    public async Task PasteCopy_IntoAnotherScene_KeepsUncopiedNodeValues()
    {
        using var source = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var other = new SceneBuilder().WithId(SceneId.FromValue(SampleScene.Tagged(0, "07E"))).Named("Other").Node(OtherRoot, "Root").Build();
        using var target = await AuthoringFixture.CreateAsync(other).ConfigureAwait(false);
        var clipboard = Capture(source, [SampleScene.Script], []);
        var paste = BuildPaste(target, clipboard, new PasteTarget(), cut: false);

        var result = await target.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var pasted = target.Scene.AllNodes.Single(node => node.Id != OtherRoot);
        var slot = pasted.Components.OfType<ScriptingComponent>().Single().Slots.Should().ContainSingle().Which;
        _ = slot.Overrides["target"].NodeId.Should().Be(SampleScene.B);
        _ = target.Scene.AllNodes.Select(static node => node.Id).Should().NotContain(SampleScene.B);
    }

    [TestMethod]
    public async Task PasteCut_MovesEntriesAndNodesAsOneEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.B, SampleScene.Cam], []);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(Folder: SampleScene.AF), cut: true);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().HaveCount(before.UndoCount + 1);
        _ = fixture.Node(SampleScene.B).Parent!.Id.Should().Be(SampleScene.A);
        _ = fixture.Node(SampleScene.Cam).Parent!.Id.Should().Be(SampleScene.A);
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.InFolder(SampleScene.AF, 0)).Should().Equal(
            LayoutEntryId.Of(SampleScene.A2),
            LayoutEntryId.Of(SampleScene.B),
            LayoutEntryId.Of(SampleScene.Cam));

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    public async Task PasteCut_FolderAcrossScopes_KeepsLocalAndNesting()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        NodePose.Pose(fixture.Node(SampleScene.B));
        var localBefore = NodePose.LocalOf(fixture.Node(SampleScene.B));
        var clipboard = Capture(fixture, [], [SampleScene.F]);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(Parent: SampleScene.A), cut: true);

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LayoutScopes.TryScopeOf(fixture.Scene, LayoutEntryId.Of(SampleScene.F), out var scope).Should().BeTrue();
        _ = scope.Should().Be(SampleScene.A);
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.UnderNode(SampleScene.A, 0)).Should().Contain(LayoutEntryId.Of(SampleScene.F));
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.InFolder(SampleScene.F, 0)).Should().Equal(LayoutEntryId.Of(SampleScene.B), LayoutEntryId.Of(SampleScene.G));
        _ = fixture.Node(SampleScene.B).Parent!.Id.Should().Be(SampleScene.A);
        _ = NodePose.LocalOf(fixture.Node(SampleScene.B)).Should().Be(localBefore);
        _ = LayoutQuery.Folder(fixture.Scene, SampleScene.F)!.IsExpanded.Should().BeTrue();
        _ = LayoutQuery.Folder(fixture.Scene, SampleScene.G)!.IsExpanded.Should().BeFalse();
    }

    [TestMethod]
    [DynamicData(nameof(OwnDescendants))]
    public async Task PasteCut_IntoOwnDescendant_IsRefused(PasteCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, row.Nodes, row.Folders);
        var before = fixture.Snapshot();

        var paste = BuildPaste(fixture, clipboard, row.Target, cut: true);

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        // A moved node's cycle names the node; a folder's names none.
        SceneAssert.Invalid(result, SceneEditCodes.HierarchyCycle, row.Nodes.Count > 0 ? row.Nodes[0] : null);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task PasteCut_SourceRemoved_FailsToBuild()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.B], []);
        SceneAssert.Applied(await fixture.Authoring.ExecuteAsync(new RemoveNodes([SampleScene.B])).ConfigureAwait(false));
        var before = fixture.Snapshot();

        var built = clipboard.TryPaste(fixture.Scene, new PasteTarget(Parent: SampleScene.A), cut: true, preserveWorld: false, out var paste, out var error);

        _ = built.Should().BeFalse();
        _ = paste.Should().BeNull();
        _ = (error!.IsError, error.Code, error.Node).Should().Be((true, SceneEditCodes.TargetMissing, (NodeId?)SampleScene.B));
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task PasteCut_SourceLocked_IsRefused()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.B], []);
        fixture.Interaction!.Lock(SampleScene.B);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(Parent: SampleScene.A), cut: true);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Refused(result, SceneEditCodes.TargetLocked, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task PasteCopy_LockedSource_Pastes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.B], []);
        fixture.Interaction!.Lock(SampleScene.B);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(), cut: false);

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.AllNodes.Count(static node => string.Equals(node.Name, "B", StringComparison.Ordinal)).Should().Be(2);
    }

    [TestMethod]
    public async Task TryPaste_NodeAndFolderOfDifferentScopes_Fails()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.A2], [SampleScene.G]);
        var before = fixture.Snapshot();

        var built = clipboard.TryPaste(fixture.Scene, new PasteTarget(), cut: true, preserveWorld: false, out var paste, out var error);

        _ = built.Should().BeFalse();
        _ = paste.Should().BeNull();
        _ = (error!.IsError, error.Code).Should().Be((true, SceneEditCodes.LayoutScopeMismatch));
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task PasteCopy_EmptyFolder_PastesAndUndoes()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [], [SampleScene.G]);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(), cut: false);
        var before = fixture.Snapshot();
        var rootBefore = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0));

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var added = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.AtRoot(0)).Except(rootBefore).Should().ContainSingle().Which;
        var copy = added.Folder!.Value;
        _ = copy.Should().NotBe(SampleScene.G);
        _ = LayoutQuery.Folder(fixture.Scene, copy)!.Name.Should().Be("G");
        _ = LayoutScopes.EntriesOf(fixture.Scene, LayoutPosition.InFolder(copy, 0)).Should().BeEmpty();

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
    }

    [TestMethod]
    [DataRow(false, DisplayName = "Copy")]
    [DataRow(true, DisplayName = "Cut")]
    public async Task Paste_IsOneUndoableStep(bool cut)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var clipboard = Capture(fixture, [SampleScene.Cam], [SampleScene.G]);
        var paste = BuildPaste(fixture, clipboard, new PasteTarget(Folder: SampleScene.AF), cut);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(paste).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.History.UndoStack.Should().HaveCount(before.UndoCount + 1);
        _ = fixture.Snapshot().Document.Should().NotBe(before.Document);

        SceneAssert.Applied(await fixture.Authoring.UndoAsync().ConfigureAwait(false));
        _ = fixture.Snapshot().Document.Should().Be(before.Document);
        _ = fixture.History.UndoStack.Should().HaveCount(before.UndoCount);
    }

    private static LayoutEntryId IdOf(ExplorerEntryData entry)
        => entry.NodeId is { } node ? LayoutEntryId.Of(node) : LayoutEntryId.Of(entry.FolderId!.Value);

    private static SceneClipboard Capture(AuthoringFixture fixture, IReadOnlyList<NodeId> nodes, IReadOnlyList<FolderId> folders)
        => SceneClipboard.Capture(fixture.Scene, nodes, folders) ?? throw new InvalidOperationException("The selection captured nothing.");

    private static SceneEditBatch BuildPaste(AuthoringFixture fixture, SceneClipboard clipboard, PasteTarget target, bool cut, bool preserveWorld = false)
    {
        var built = clipboard.TryPaste(fixture.Scene, target, cut, preserveWorld, out var edit, out var error);
        _ = built.Should().BeTrue($"the paste must build, but: {error?.Code}");
        return edit!;
    }
}
