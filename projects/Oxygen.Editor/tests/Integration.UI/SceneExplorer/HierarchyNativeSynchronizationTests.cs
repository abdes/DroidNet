// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using static Oxygen.Editor.TestSupport.NativeFieldCases;
using static Oxygen.Editor.TestSupport.NativeSceneAssertions;

namespace Oxygen.Editor.Integration.UI.Tests.SceneExplorer;

/// <summary>Verifies native local TRS through authored hierarchy command history.</summary>
[TestClass]
public sealed partial class HierarchyNativeSynchronizationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Gets or sets the test execution context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Reparenting and grouping publish local TRS at commit, undo and redo.</summary>
    /// <param name="groupIntoFolder">Whether to group across node scopes instead of preserve-world reparenting.</param>
    /// <param name="ignoreParentTransform">Whether the authored node ignores its parent's transform.</param>
    /// <returns>The asynchronous native observation test.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task HierarchyCommandUndoRedoConvergesNativeLocalTrs(bool groupIntoFolder, bool ignoreParentTransform) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(
            automatic: false,
            scene => SeedHierarchy(scene, ignoreParentTransform),
            layout: HierarchyLayout);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var source = fixture.Source.RootNodes[0];
        var destination = fixture.Source.RootNodes[1];
        var moved = source.Hierarchy.Children.Single();
        var folderId = fixture.Layout[1].Children!.Single().FolderId!.Value;
        var before = SourceNodeProperties(moved);
        await AssertNativeLocalTrsAsync(fixture, moved.Id, before, timeout.Token).ConfigureAwait(true);

        // Grouping into another node's folder is the Explorer's batch: reparent keeping the local transform, then move the entry.
        ISceneEdit edit = groupIntoFolder
            ? new SceneEditBatch("Group", [new ReparentNodes([moved.Id], destination.Id, preserveWorld: false), new MoveLayoutEntries([LayoutEntryId.Of(moved.Id)], LayoutPosition.InFolder(folderId, 0))])
            : new ReparentNodes([moved.Id], destination.Id, preserveWorld: true);
        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(true);

        _ = result.Succeeded.Should().BeTrue();
        _ = moved.Hierarchy.Parent.Should().BeSameAs(destination);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        var after = SourceNodeProperties(moved);
        if (groupIntoFolder || ignoreParentTransform)
        {
            _ = after.Should().BeEquivalentTo(before, "grouping and ignored-parent moves preserve local TRS");
        }
        else
        {
            _ = after[(1, 0)].Should().NotBe(before[(1, 0)], "ordinary preserve-world reparenting must compensate the local position");
        }

        await AssertNativeLocalTrsAsync(fixture, moved.Id, after, timeout.Token).ConfigureAwait(true);

        await fixture.Document.History.UndoAsync(timeout.Token).ConfigureAwait(true);

        _ = moved.Hierarchy.Parent.Should().BeSameAs(source);
        _ = SourceNodeProperties(moved).Should().BeEquivalentTo(before);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = fixture.Document.History.RedoStack.Should().ContainSingle();
        await AssertNativeLocalTrsAsync(fixture, moved.Id, before, timeout.Token).ConfigureAwait(true);

        await fixture.Document.History.RedoAsync(timeout.Token).ConfigureAwait(true);

        _ = moved.Hierarchy.Parent.Should().BeSameAs(destination);
        _ = SourceNodeProperties(moved).Should().BeEquivalentTo(after);
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        _ = fixture.Document.History.RedoStack.Should().BeEmpty();
        await AssertNativeLocalTrsAsync(fixture, moved.Id, after, timeout.Token).ConfigureAwait(true);
    });

    private static void SeedHierarchy(Scene scene, bool ignoreParentTransform)
    {
        var source = new SceneNode(scene, "Source");
        var sourceTransform = source.Transform;
        sourceTransform.LocalPosition = new Vector3(10f, 2f, -4f);
        sourceTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.2f, 0.1f, 0.3f);
        sourceTransform.LocalScale = new Vector3(2f);
        var destination = new SceneNode(scene, "Destination");
        var destinationTransform = destination.Transform;
        destinationTransform.LocalPosition = new Vector3(-3f, 4f, 7f);
        destinationTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(-0.3f, 0.2f, -0.1f);
        destinationTransform.LocalScale = new Vector3(3f);
        var moved = new SceneNode(scene, "Moved");
        moved.Flags.IgnoreParentTransform = ignoreParentTransform;
        var transform = moved.Transform;
        transform.LocalPosition = new Vector3(1f, 2f, 3f);
        transform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.1f, -0.2f, 0.3f);
        transform.LocalScale = new Vector3(1f, 2f, 3f);
        source.Hierarchy.AddChild(moved);
        scene.RootNodes.Add(source);
        scene.RootNodes.Add(destination);
    }

    // The source holds the moved node; the destination holds an empty folder the moved node can be grouped into.
    private static IList<ExplorerEntryData> HierarchyLayout(Scene scene)
    {
        var source = scene.RootNodes[0];
        var destination = scene.RootNodes[1];
        return
        [
            new ExplorerEntryData
            {
                Type = "Node",
                NodeId = source.Id,
                Children = [new ExplorerEntryData { Type = "Node", NodeId = source.Hierarchy.Children.Single().Id }],
            },
            new ExplorerEntryData
            {
                Type = "Node",
                NodeId = destination.Id,
                Children =
                [
                    new ExplorerEntryData
                    {
                        Type = "Folder",
                        FolderId = FolderId.New(),
                        Name = "Group",
                        Children = [],
                    },
                ],
            },
        ];
    }

    private static async Task AssertNativeLocalTrsAsync(
        NativeSceneFixture fixture,
        NodeId nodeId,
        IReadOnlyDictionary<(ushort component, ushort field), RuntimeFieldValue> expected,
        CancellationToken cancellationToken)
    {
        // ObserveNode reads stored local TRS, not native parenting, world pose or the ignored-parent flag.
        var state = await WaitForNodeAsync(
            fixture,
            nodeId,
            state => state.Exists && expected.All(field => state.Properties.Any(
                actual => (ushort)actual.ComponentId == field.Key.component
                    && actual.FieldId == field.Key.field
                    && Matches(actual.Value, field.Value, 0.001f))),
            cancellationToken).ConfigureAwait(true);
        _ = state.Exists.Should().BeTrue();
        _ = state.Properties.Where(property => property.ComponentId == RuntimeComponentId.Transform).Should().HaveCount(9);
        foreach (var field in expected)
        {
            var actual = state.Properties.Single(property => (ushort)property.ComponentId == field.Key.component && property.FieldId == field.Key.field);
            _ = Matches(actual.Value, field.Value, 0.001f).Should().BeTrue("native local TRS field {0} is {1}, not {2}", field.Key.field, actual.Value, field.Value);
        }
    }
}
