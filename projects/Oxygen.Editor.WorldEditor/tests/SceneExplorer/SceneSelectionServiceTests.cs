// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

[TestClass]
[TestCategory("Scene Selection")]
public sealed class SceneSelectionServiceTests
{
    [TestMethod]
    public void SetSelection_DeduplicatesNodesAndPreservesOrder()
    {
        var scene = CreateScene();
        var first = new SceneNode(scene) { Name = "First" };
        var second = new SceneNode(scene) { Name = "Second" };
        scene.RootNodes.Add(first);
        scene.RootNodes.Add(second);
        var sut = new SceneSelectionService();

        sut.SetSelection(scene.Id, [first, second, first], "test");

        _ = sut.GetSelectedNodes(scene.Id, scene).Should().Equal(first, second);
    }

    [TestMethod]
    public void Reconcile_RemovesNodesThatNoLongerBelongToScene()
    {
        var scene = CreateScene();
        var staleScene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        var stale = new SceneNode(staleScene) { Name = "Stale" };
        scene.RootNodes.Add(selected);
        staleScene.RootNodes.Add(stale);
        var sut = new SceneSelectionService();
        sut.SetSelection(scene.Id, [selected, stale], "test");

        var reconciled = sut.Reconcile(scene.Id, scene);

        _ = reconciled.Should().Equal(selected);
        _ = sut.GetSelectedNodes(scene.Id, scene).Should().Equal(selected);
    }

    [TestMethod]
    public void Clear_RemovesDocumentSelection()
    {
        var scene = CreateScene();
        var selected = new SceneNode(scene) { Name = "Selected" };
        scene.RootNodes.Add(selected);
        var sut = new SceneSelectionService();
        sut.SetSelection(scene.Id, [selected], "test");

        sut.Clear(scene.Id);

        _ = sut.GetSelectedNodes(scene.Id, scene).Should().BeEmpty();
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>().Object;
        return new Scene(project) { Name = "Test Scene" };
    }
}
