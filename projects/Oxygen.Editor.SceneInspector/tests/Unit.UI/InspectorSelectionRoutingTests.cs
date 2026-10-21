// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Oxygen.Editor.SceneInspector.TestSupport;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>
/// T5/C16: the Inspector routes by the classified selection context — folders and mixed
/// batches reach the grouping summary (never the environment or a node-subset component
/// edit), the root row reaches the scene Environment, and node selections keep the
/// component editors.
/// </summary>
[TestClass]
internal sealed class InspectorSelectionRoutingTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A folder-only selection shows the grouping summary instead of Environment or components.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task FolderSelectionShowsGroupingSummaryNotEnvironment() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Environment");
        var folderId = FolderId.New();
        fixture.Selection.Publish(
            fixture.TestScene.DocumentId,
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId),
            "Test");
        _ = host.HasSelectionSummary.Should().BeTrue();
        _ = host.InspectorTitle.Should().Be("Grouping Summary");
        _ = host.HasEnvironmentView.Should().BeFalse("a folder selection is not the scene context");
        _ = host.HasItems.Should().BeFalse("folder identities must not be reduced to node entities");
        _ = host.PropertyEditors.Should().BeEmpty();
        _ = host.SelectionSummaryText.Should().Contain("grouping");
        return Task.CompletedTask;
    });

    /// <summary>A mixed node/folder batch shows the aggregate summary, not the node subset as a component edit.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task MixedSelectionShowsAggregateNotComponentEditors() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Transform");
        var folderId = FolderId.New();
        fixture.Selection.Publish(
            fixture.TestScene.DocumentId,
            new SceneSelectionContext(SceneSelectionKind.Mixed, [fixture.Node.Id], [folderId], fixture.Node.Id, PrimaryFolderId: null),
            "Test");
        _ = host.HasSelectionSummary.Should().BeTrue();
        _ = host.InspectorTitle.Should().Be("Selection Summary");
        _ = host.PropertyEditors.Should().BeEmpty("the node subset of a mixed batch must not pose as the whole selection");
        _ = host.SelectionSummaryText.Should().Contain("1 objects and 1 groupings");
        return Task.CompletedTask;
    });

    /// <summary>The root row selection routes to the scene Environment surface.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task RootSelectionShowsSceneEnvironment() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Environment");
        fixture.Selection.Publish(
            fixture.TestScene.DocumentId,
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNodeId: null, PrimaryFolderId: null),
            "Test");
        _ = host.HasEnvironmentView.Should().BeTrue();
        _ = host.HasSelectionSummary.Should().BeFalse();
        _ = host.InspectorTitle.Should().Be("Scene Inspector");
        _ = host.PropertyEditors.Should().Contain(host.EnvironmentEditor);
        return Task.CompletedTask;
    });

    /// <summary>Plain node selections keep the component inspector behaviour unchanged.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task NodeSelectionKeepsComponentInspector() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Transform");
        fixture.Select(fixture.Node);
        _ = host.HasSelectionSummary.Should().BeFalse();
        _ = host.HasEnvironmentView.Should().BeFalse();
        _ = host.InspectorTitle.Should().Be("Component Inspector");
        _ = host.HasItems.Should().BeTrue();
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SelectionOfAnotherDocumentDoesNotChangeInspector() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        fixture.Select(fixture.Node);
        using var host = fixture.CreateInspectorHost("Transform");
        var folderId = FolderId.New();

        fixture.Selection.Publish(
            DocumentId.New(),
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId),
            "Test");

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.HasSelectionSummary.Should().BeFalse();
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SupersededSelectionChangeDoesNotReplaceCurrentSelection() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        var folderId = FolderId.New();
        var previous = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId);

        // A subscriber that runs before the Inspector replaces the folder selection while it is still being announced:
        // the Inspector then sees a change whose context the service no longer holds.
        var superseded = false;
        fixture.Selection.SelectionChanged += (_, args) =>
        {
            if (!superseded && ReferenceEquals(args.Context, previous))
            {
                superseded = true;
                fixture.Select(fixture.Node);
            }
        };
        using var host = fixture.CreateInspectorHost("Transform");

        fixture.Selection.Publish(fixture.TestScene.DocumentId, previous, "Test");

        _ = superseded.Should().BeTrue();

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.InspectorTitle.Should().Be("Component Inspector");
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task UnresolvedNodeSelectionDoesNotExposeSceneEnvironment() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Transform");
        var missingNodeId = NodeId.New();
        var context = new SceneSelectionContext(SceneSelectionKind.Node, [missingNodeId], [], missingNodeId, PrimaryFolderId: null);
        fixture.Selection.Publish(fixture.TestScene.DocumentId, context, "Test");

        _ = host.HasEnvironmentView.Should().BeFalse();
        _ = host.PropertyEditors.Should().BeEmpty();
        _ = host.HasSelectionNotice.Should().BeTrue();
        _ = host.SelectionSummaryText.Should().Contain("not available");
        _ = host.InspectorTitle.Should().Be("Component Inspector");
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task FolderSelectionThenNodeSelectionRestoresComponentEditors() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Transform");
        var folderId = FolderId.New();
        var folderContext = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId);
        fixture.Selection.Publish(fixture.TestScene.DocumentId, folderContext, "Test");
        _ = host.PropertyEditors.Should().BeEmpty();

        fixture.Select(fixture.Node);

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.PropertyEditors.Should().NotBeEmpty();
        _ = host.HasEnvironmentView.Should().BeFalse();
        _ = host.HasSelectionSummary.Should().BeFalse();
        return Task.CompletedTask;
    });
}
