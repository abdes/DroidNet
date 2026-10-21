// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Problems.Unit.Tests;

/// <summary>A runtime problem's node is shown in its scene document and selected through the scene selection.</summary>
[TestClass]
internal sealed class RuntimeProblemNodesTests
{
    private static readonly RuntimeScriptProblem Problem = new(DiagnosticSeverity.Error, "S0701", "attempt to index nil", "rendered") { SlotNode = "Cube", SlotIndex = 0 };

    /// <summary>The node the problem names is selected in its scene document, which is shown first.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task NodeIsSelectedInItsShownSceneAsync()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\ProblemsTests"));
        var document = await OpenSceneAsync(documents).ConfigureAwait(false);
        var node = SceneContent.Of(document).Scene.RootNodes.Single();
        var area = AreaOf(document);
        var selection = new SceneSelectionService();
        var nodes = new RuntimeProblemNodes(RuntimeOf(document.Id, node.Id), area.Object, selection);

        var selected = await nodes.SelectAsync(Problem).ConfigureAwait(false);

        _ = selected.Should().BeTrue();
        area.Verify(shown => shown.ShowAsync(It.Is<Document>(shownDocument => ReferenceEquals(shownDocument, document))), Times.Once);
        _ = selection.GetSelectedNodes(document.Id, SceneContent.Of(document).Scene).Should().ContainSingle().Which.Id.Should().Be(node.Id);
    }

    /// <summary>A problem whose scene has no tab, or whose node is gone, selects nothing.</summary>
    /// <returns>A task that completes when the test has run.</returns>
    [TestMethod]
    public async Task ProblemWithoutItsSceneOrNodeSelectsNothingAsync()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem(), @"C:\ProblemsTests"));
        var document = await OpenSceneAsync(documents).ConfigureAwait(false);
        var selection = new SceneSelectionService();
        var noTab = new Mock<IDocumentArea>();
        _ = noTab.SetupGet(static area => area.Documents).Returns([]);

        var withoutTab = await new RuntimeProblemNodes(RuntimeOf(document.Id, NodeId.New()), noTab.Object, selection).SelectAsync(Problem).ConfigureAwait(false);
        var withoutNode = await new RuntimeProblemNodes(RuntimeOf(document.Id, NodeId.New()), AreaOf(document).Object, selection).SelectAsync(Problem).ConfigureAwait(false);

        _ = withoutTab.Should().BeFalse();
        _ = withoutNode.Should().BeFalse();
        _ = selection.GetContext(document.Id).SelectedNodeIds.Should().BeEmpty();
    }

    private static async Task<Document> OpenSceneAsync(TestSceneDocuments documents)
    {
        var built = new Scene { Name = "Test Scene" };
        built.RootNodes.Add(new SceneNode(built) { Name = "Cube" });
        return await documents.OpenAsync(built.Dehydrate()).ConfigureAwait(false);
    }

    private static Mock<IDocumentArea> AreaOf(Document document)
    {
        var area = new Mock<IDocumentArea>();
        _ = area.SetupGet(static shown => shown.Documents).Returns([document]);
        _ = area.Setup(shown => shown.ShowAsync(document)).ReturnsAsync(value: true);
        return area;
    }

    private static IRuntimeProblemSource RuntimeOf(DocumentId document, NodeId node)
    {
        var runtime = new Mock<IRuntimeProblemSource>();
        _ = runtime.Setup(static source => source.NodeOf(Problem)).Returns((document, node));
        return runtime.Object;
    }
}
