// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneExplorer;

/// <summary>Ensures the context-menu shape and eligibility cover every row kind.</summary>
[TestClass]
public sealed class SceneExplorerContextMenuTests
{
    [TestMethod]
    public void Build_ForNode_ContainsCoreEditActions()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Node, primaryIsInFolder: false, primaryHasChildren: false, primaryIsUnlocked: true);

        _ = menu.Select(entry => entry.Kind).Should().Contain(
            new[]
            {
                SceneExplorerCommandKind.NewNode,
                SceneExplorerCommandKind.Rename,
                SceneExplorerCommandKind.Cut,
                SceneExplorerCommandKind.Copy,
                SceneExplorerCommandKind.Paste,
                SceneExplorerCommandKind.PasteAsChild,
                SceneExplorerCommandKind.Delete,
            });
    }

    [TestMethod]
    public void Build_ForLockedNode_DisablesDestructiveActions()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Node, primaryIsInFolder: false, primaryHasChildren: false, primaryIsUnlocked: false);

        _ = menu.Single(entry => entry.Kind == SceneExplorerCommandKind.Rename).IsEnabled.Should().BeFalse();
        _ = menu.Single(entry => entry.Kind == SceneExplorerCommandKind.Cut).IsEnabled.Should().BeFalse();
        _ = menu.Single(entry => entry.Kind == SceneExplorerCommandKind.Delete).IsEnabled.Should().BeFalse();
        _ = menu.Single(entry => entry.Kind == SceneExplorerCommandKind.Copy).IsEnabled.Should().BeTrue();
    }

    [TestMethod]
    public void Build_ForFolder_ContainsRemoveFolderButNotPasteAsChild()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Folder, primaryIsInFolder: false, primaryHasChildren: false, primaryIsUnlocked: true);

        _ = menu.Select(entry => entry.Kind).Should().Contain(SceneExplorerCommandKind.NewNode);
        _ = menu.Select(entry => entry.Kind).Should().Contain(SceneExplorerCommandKind.Delete);
        _ = menu.Select(entry => entry.Kind).Should().NotContain(SceneExplorerCommandKind.PasteAsChild);
    }

    [TestMethod]
    public void Build_ForMixed_ContainsOnlySharedActions()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Mixed, primaryIsInFolder: false, primaryHasChildren: false, primaryIsUnlocked: true);

        _ = menu.Select(entry => entry.Kind).Should().Contain(
            new[]
            {
                SceneExplorerCommandKind.Cut,
                SceneExplorerCommandKind.Copy,
                SceneExplorerCommandKind.Paste,
                SceneExplorerCommandKind.Delete,
            });
        _ = menu.Select(entry => entry.Kind).Should().NotContain(SceneExplorerCommandKind.Rename);
        _ = menu.Select(entry => entry.Kind).Should().NotContain(SceneExplorerCommandKind.NewNode);
    }

    [TestMethod]
    public void Build_ForSceneRoot_ContainsRenameCreationAndPasteButNotDelete()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Scene, primaryIsInFolder: false, primaryHasChildren: false, primaryIsUnlocked: true);

        _ = menu.Select(entry => entry.Kind).Should().Contain(
            new[]
            {
                SceneExplorerCommandKind.NewNode,
                SceneExplorerCommandKind.NewFolder,
                SceneExplorerCommandKind.Rename,
                SceneExplorerCommandKind.Paste,
            });
        _ = menu.Select(entry => entry.Kind).Should().NotContain(SceneExplorerCommandKind.Delete);
    }

    [TestMethod]
    public void Build_ForNodeInFolder_OffersRemoveFromFolder()
    {
        var menu = SceneExplorerContextMenu.Build(SceneSelectionKind.Node, primaryIsInFolder: true, primaryHasChildren: false, primaryIsUnlocked: true);

        _ = menu.Select(entry => entry.Kind).Should().Contain(SceneExplorerCommandKind.RemoveFromFolder);
        _ = menu.Select(entry => entry.Kind).Should().NotContain(SceneExplorerCommandKind.MoveToSceneRoot);
    }
}
