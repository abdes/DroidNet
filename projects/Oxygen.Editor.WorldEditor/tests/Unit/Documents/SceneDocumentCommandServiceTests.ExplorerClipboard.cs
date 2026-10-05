// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task ExplorerClipboard_CopyMixedNestedFolders_DuplicatesAllOnceAndUndoRedoIsAtomic()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "Grouped" };
        var child = new SceneNode(scene) { Name = "Child" };
        first.AddChild(child);
        var second = new SceneNode(scene) { Name = "Standalone" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var folderId = Guid.NewGuid();
        var nestedId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData
            {
                Type = "Folder", FolderId = folderId, Name = "Group",
                Children =
                [
                    new ExplorerEntryData
                    {
                        Type = "Folder", FolderId = nestedId, Name = "Nested",
                        Children = [new ExplorerEntryData { NodeId = first.Id }],
                    },
                ],
            },
            new ExplorerEntryData { NodeId = second.Id },
        ]);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [first.Id, child.Id, second.Id], [folderId, nestedId]);
        _ = payload.Succeeded.Should().BeTrue();
        _ = payload.Value!.Entries.Should().HaveCount(2);
        _ = payload.Value.Nodes.Should().HaveCount(2);
        first.Name = "Source changed after capture";

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload.Value, false, null, null, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.RootNodes.Should().HaveCount(4);
        _ = scene.RootNodes[2].Name.Should().Be("Grouped");
        _ = scene.RootNodes[2].Children.Should().ContainSingle();
        _ = scene.ExplorerLayout!.Last(entry => entry.Type == "Folder").FolderId.Should().NotBe(folderId);
        _ = context.History.UndoStack.Should().ContainSingle();
        var after = SerializeHierarchy(scene);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.RootNodes.Should().HaveCount(2);
        _ = scene.ExplorerLayout.Should().HaveCount(2);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SerializeHierarchy(scene).Should().Be(after);
        _ = before.Should().NotBe(after);
    }

    [TestMethod]
    public async Task ExplorerClipboard_CutFolderAcrossNodeScope_PreservesLocalAndNestedLineageWithOneUndo()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var oldParent = new SceneNode(scene) { Name = "Old parent" };
        var newParent = new SceneNode(scene) { Name = "New parent" };
        var grouped = new SceneNode(scene) { Name = "Grouped" };
        grouped.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(1, 2, 3);
        oldParent.AddChild(grouped);
        scene.RootNodes.Add(oldParent);
        scene.RootNodes.Add(newParent);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData
            {
                NodeId = oldParent.Id,
                Children =
                [
                    new ExplorerEntryData
                    {
                        Type = "Folder", FolderId = folderId, Name = "Group",
                        Children = [new ExplorerEntryData { NodeId = grouped.Id }],
                    },
                ],
            },
            new ExplorerEntryData { NodeId = newParent.Id },
        ]);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [], [folderId]).Value!;

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, true, newParent.Id, null, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = grouped.Parent.Should().BeSameAs(newParent);
        _ = grouped.Components.OfType<TransformComponent>().Single().LocalPosition.Should().Be(new Vector3(1, 2, 3));
        _ = scene.ExplorerLayout![1].Children!.Single().FolderId.Should().Be(folderId);
        _ = context.History.UndoStack.Should().ContainSingle();
        var after = SerializeHierarchy(scene);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SerializeHierarchy(scene).Should().Be(before);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = SerializeHierarchy(scene).Should().Be(after);
    }

    [TestMethod]
    public async Task ExplorerClipboard_StaleMixedCut_RejectsWithoutPartialMutation()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var node = new SceneNode(scene) { Name = "Node" };
        scene.RootNodes.Add(node);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout([new ExplorerEntryData { NodeId = node.Id },
            new ExplorerEntryData { Type = "Folder", FolderId = folderId, Name = "Empty" }]);
        var context = CreateContext(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [node.Id], [folderId]).Value!;
        scene.SetExplorerLayout([new ExplorerEntryData { NodeId = node.Id }]);
        var before = SerializeHierarchy(scene);

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, true, null, null, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ExplorerClipboard_FolderIntoDescendant_RejectsWithoutHistory()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var parentId = Guid.NewGuid();
        var childId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData
            {
                Type = "Folder", FolderId = parentId, Name = "Parent",
                Children = [new ExplorerEntryData { Type = "Folder", FolderId = childId, Name = "Child" }],
            },
        ]);
        var context = CreateContext(scene);
        var before = SerializeHierarchy(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [], [parentId]).Value!;

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, true, null, childId, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task ExplorerClipboard_CutBatchWithLockedNonPrimaryNode_RejectsEntireTransaction()
    {
        var (interaction, _) = CreateInteraction();
        var fixture = CreateFixture(interaction: interaction);
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout(
        [
            new ExplorerEntryData { NodeId = first.Id },
            new ExplorerEntryData
            {
                Type = "Folder", FolderId = folderId, Name = "Group",
                Children = [new ExplorerEntryData { NodeId = second.Id }],
            },
        ]);
        var context = CreateContext(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [first.Id], [folderId]).Value!;
        interaction.SetLocked(second.Id, true);
        var before = SerializeHierarchy(scene);

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, true, null, null, false).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = SerializeHierarchy(scene).Should().Be(before);
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Sut.ValidateExplorerPaste(context, payload, true, null, null, false).Should().Contain("Second");
    }

    [TestMethod]
    [DataRow(false, 2f)]
    [DataRow(true, -8f)]
    public async Task ExplorerClipboard_CopyIntoNode_UsesExplicitTransformIntent(bool preserveWorld, float expectedLocalX)
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var source = new SceneNode(scene) { Name = "Source" };
        source.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(2, 0, 0);
        var parent = new SceneNode(scene) { Name = "Destination" };
        parent.Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(10, 0, 0);
        scene.RootNodes.Add(source);
        scene.RootNodes.Add(parent);
        var context = CreateContext(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [source.Id], []).Value!;

        var result = await fixture.Sut.PasteExplorerItemsAsync(context, payload, false, parent.Id, null, preserveWorld).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = parent.Children.Should().ContainSingle().Which.Components.OfType<TransformComponent>()
            .Single().LocalPosition.X.Should().Be(expectedLocalX);
        _ = source.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(2f);
    }

    [TestMethod]
    public async Task ExplorerClipboard_EmptyFolderCopy_IsReusableAndUndoable()
    {
        var fixture = CreateFixture();
        ConfigureHierarchySync(fixture);
        var scene = CreateScene();
        var folderId = Guid.NewGuid();
        scene.SetExplorerLayout([new ExplorerEntryData { Type = "Folder", FolderId = folderId, Name = "Empty" }]);
        var context = CreateContext(scene);
        var payload = fixture.Sut.CaptureExplorerClipboard(context, [], [folderId]).Value!;
        _ = payload.Nodes.Should().BeEmpty();

        _ = (await fixture.Sut.PasteExplorerItemsAsync(context, payload, false, null, null, false).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = (await fixture.Sut.PasteExplorerItemsAsync(context, payload, false, null, null, false).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = scene.ExplorerLayout.Should().HaveCount(3);
        _ = scene.ExplorerLayout!.Select(entry => entry.FolderId).Should().OnlyHaveUniqueItems();
        _ = context.History.UndoStack.Should().HaveCount(2);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.ExplorerLayout.Should().HaveCount(2);
    }
}
