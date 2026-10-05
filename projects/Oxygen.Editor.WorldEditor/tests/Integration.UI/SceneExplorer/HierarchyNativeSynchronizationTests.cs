// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.SceneExplorer;

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
            hierarchyAuthoring: true);
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        var source = fixture.Source.RootNodes[0];
        var destination = fixture.Source.RootNodes[1];
        var moved = source.Children.Single();
        var folderId = fixture.Source.ExplorerLayout![1].Children!.Single().FolderId!.Value;
        var before = SourceNodeProperties(moved);
        await AssertNativeLocalTrsAsync(fixture, moved.Id, before, timeout.Token).ConfigureAwait(true);

        var result = groupIntoFolder
            ? await fixture.Commands.MoveNodesToFolderAsync(fixture.Context, [moved.Id], folderId).ConfigureAwait(true)
            : await fixture.Commands.ReparentNodesAsync(fixture.Context, [moved.Id], destination.Id, preserveWorldTransform: true).ConfigureAwait(true);

        _ = result.Succeeded.Should().BeTrue();
        _ = moved.Parent.Should().BeSameAs(destination);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
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

        await fixture.Context.History.UndoAsync(timeout.Token).ConfigureAwait(true);

        _ = moved.Parent.Should().BeSameAs(source);
        _ = SourceNodeProperties(moved).Should().BeEquivalentTo(before);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
        _ = fixture.Context.History.RedoStack.Should().ContainSingle();
        await AssertNativeLocalTrsAsync(fixture, moved.Id, before, timeout.Token).ConfigureAwait(true);

        await fixture.Context.History.RedoAsync(timeout.Token).ConfigureAwait(true);

        _ = moved.Parent.Should().BeSameAs(destination);
        _ = SourceNodeProperties(moved).Should().BeEquivalentTo(after);
        _ = fixture.Context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Context.History.RedoStack.Should().BeEmpty();
        await AssertNativeLocalTrsAsync(fixture, moved.Id, after, timeout.Token).ConfigureAwait(true);
    });

    private static void SeedHierarchy(Scene scene, bool ignoreParentTransform)
    {
        var source = new SceneNode(scene) { Name = "Source" };
        var sourceTransform = source.Components.OfType<TransformComponent>().Single();
        sourceTransform.LocalPosition = new Vector3(10f, 2f, -4f);
        sourceTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.2f, 0.1f, 0.3f);
        sourceTransform.LocalScale = new Vector3(2f);
        var destination = new SceneNode(scene) { Name = "Destination" };
        var destinationTransform = destination.Components.OfType<TransformComponent>().Single();
        destinationTransform.LocalPosition = new Vector3(-3f, 4f, 7f);
        destinationTransform.LocalRotation = Quaternion.CreateFromYawPitchRoll(-0.3f, 0.2f, -0.1f);
        destinationTransform.LocalScale = new Vector3(3f);
        var moved = new SceneNode(scene) { Name = "Moved", IgnoreParentTransform = ignoreParentTransform };
        var transform = moved.Components.OfType<TransformComponent>().Single();
        transform.LocalPosition = new Vector3(1f, 2f, 3f);
        transform.LocalRotation = Quaternion.CreateFromYawPitchRoll(0.1f, -0.2f, 0.3f);
        transform.LocalScale = new Vector3(1f, 2f, 3f);
        source.AddChild(moved);
        scene.RootNodes.Add(source);
        scene.RootNodes.Add(destination);
        scene.SetExplorerLayout(
            [
                new ExplorerEntryData
                {
                    NodeId = source.Id,
                    Children = [new ExplorerEntryData { NodeId = moved.Id }],
                },
                new ExplorerEntryData
                {
                    NodeId = destination.Id,
                    Children =
                    [
                        new ExplorerEntryData
                        {
                            Type = "Folder",
                            FolderId = Guid.NewGuid(),
                            Name = "Group",
                            Children = [],
                        },
                    ],
                },
            ]);
    }

    private static async Task AssertNativeLocalTrsAsync(
        NativeSceneFixture fixture,
        Guid nodeId,
        IReadOnlyDictionary<(ushort component, ushort field), float> expected,
        CancellationToken cancellationToken)
    {
        // ObserveNode reads stored local TRS, not native parenting, world pose or the ignored-parent flag.
        var state = await WaitForNodeAsync(
            fixture,
            nodeId,
            state => state.Exists && expected.All(field => state.Properties.Any(
                actual => actual.ComponentId == field.Key.component
                    && actual.FieldId == field.Key.field
                    && MathF.Abs(actual.Value - field.Value) <= 0.001f)),
            cancellationToken).ConfigureAwait(true);
        _ = state.Exists.Should().BeTrue();
        _ = state.Properties.Where(property => property.ComponentId == 1).Should().HaveCount(9);
        foreach (var field in expected)
        {
            var actual = state.Properties.Single(property => property.ComponentId == field.Key.component && property.FieldId == field.Key.field);
            _ = actual.Value.Should().BeApproximately(field.Value, 0.001f, "native local TRS field {0}", field.Key.field);
        }
    }
}
