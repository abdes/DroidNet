// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.SceneInspector.TestSupport;

namespace Oxygen.Editor.SceneInspector.Unit.UI.Tests;

/// <summary>The node header renames its node through the scene's authoring.</summary>
[TestClass]
internal sealed class NodeHeaderRenameTests : DroidNet.Tests.VisualUserInterfaceTests
{
    /// <summary>Gets or sets the current test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A name typed in the header renames the node as one step of the scene's history.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task TypedNameRenamesTheNodeAsOneUndoableStep() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Transform");
        var header = new SceneNodeDetailsView
        {
            NodeEdits = model,
            Node = fixture.Node,
        };
        await LoadTestContentAsync(header).ConfigureAwait(true);
        var before = fixture.Node.Metadata.Name;

        header.ViewModel!.Name = "Renamed";

        _ = fixture.Node.Metadata.Name.Should().Be("Renamed");
        _ = fixture.Document.History.UndoStack.Should().ContainSingle();
        await fixture.Document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Node.Metadata.Name.Should().Be(before);
    });

    /// <summary>A name the authoring refuses leaves the node and its history, and the header shows the node's name again.</summary>
    /// <returns>The asynchronous UI test.</returns>
    [TestMethod]
    public Task RefusedNameShowsTheNodeNameAgain() => EnqueueAsync(async () =>
    {
        using var fixture = new SceneAuthoringFixture();
        using var model = fixture.CreateInspectorHost("Transform");
        var header = new SceneNodeDetailsView
        {
            NodeEdits = model,
            Node = fixture.Node,
        };
        await LoadTestContentAsync(header).ConfigureAwait(true);
        var before = fixture.Node.Metadata.Name;

        header.ViewModel!.Name = "   ";

        _ = fixture.Node.Metadata.Name.Should().Be(before);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
        _ = header.ViewModel.Name.Should().Be(before);
    });
}
