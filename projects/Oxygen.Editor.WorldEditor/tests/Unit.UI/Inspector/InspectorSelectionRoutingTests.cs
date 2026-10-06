// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.Documents.Selection;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

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
        var folderId = Guid.NewGuid();
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [],
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId)));
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
        var folderId = Guid.NewGuid();
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [fixture.Node],
            new SceneSelectionContext(SceneSelectionKind.Mixed, [fixture.Node.Id], [folderId], fixture.Node.Id, PrimaryFolderId: null)));
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
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [],
            new SceneSelectionContext(SceneSelectionKind.Scene, [], [], PrimaryNodeId: null, PrimaryFolderId: null)));
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
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [fixture.Node],
            new SceneSelectionContext(SceneSelectionKind.Node, [fixture.Node.Id], [], fixture.Node.Id, PrimaryFolderId: null)));
        _ = host.HasSelectionSummary.Should().BeFalse();
        _ = host.HasEnvironmentView.Should().BeFalse();
        _ = host.InspectorTitle.Should().Be("Component Inspector");
        _ = host.HasItems.Should().BeTrue();
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SelectionMessageForAnotherDocumentDoesNotChangeInspector() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        fixture.Selection.SetSelection(fixture.Scene.Id, [fixture.Node], "test");
        using var host = fixture.CreateInspectorHost("Transform");
        var folderId = Guid.NewGuid();

        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [],
            new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId),
            Guid.NewGuid()));

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.HasSelectionSummary.Should().BeFalse();
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task SupersededSelectionMessageDoesNotReplaceCurrentSelection() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        var folderId = Guid.NewGuid();
        var previous = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId);
        fixture.Selection.Publish(fixture.Scene.Id, previous, "test");
        fixture.Selection.SetSelection(fixture.Scene.Id, [fixture.Node], "test");
        using var host = fixture.CreateInspectorHost("Transform");

        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([], previous, fixture.Scene.Id));

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.InspectorTitle.Should().Be("Component Inspector");
        return Task.CompletedTask;
    });

    [TestMethod]
    public Task UnresolvedNodeSelectionDoesNotExposeSceneEnvironment() => EnqueueAsync(() =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var host = fixture.CreateInspectorHost("Transform");
        var missingNodeId = Guid.NewGuid();
        var context = new SceneSelectionContext(SceneSelectionKind.Node, [missingNodeId], [], missingNodeId, PrimaryFolderId: null);
        fixture.Selection.Publish(fixture.Scene.Id, context, "test");

        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([], context, fixture.Scene.Id));

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
        var folderId = Guid.NewGuid();
        var folderContext = new SceneSelectionContext(SceneSelectionKind.Folder, [], [folderId], PrimaryNodeId: null, folderId);
        fixture.Selection.Publish(fixture.Scene.Id, folderContext, "test");
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage([], folderContext, fixture.Scene.Id));
        _ = host.PropertyEditors.Should().BeEmpty();

        fixture.Selection.SetSelection(fixture.Scene.Id, [fixture.Node], "test");
        _ = fixture.Messenger.Send(new SceneNodeSelectionChangedMessage(
            [fixture.Node], fixture.Selection.GetContext(fixture.Scene.Id), fixture.Scene.Id));

        _ = host.SelectedNode.Should().BeSameAs(fixture.Node);
        _ = host.PropertyEditors.Should().NotBeEmpty();
        _ = host.HasEnvironmentView.Should().BeFalse();
        _ = host.HasSelectionSummary.Should().BeFalse();
        return Task.CompletedTask;
    });
}
