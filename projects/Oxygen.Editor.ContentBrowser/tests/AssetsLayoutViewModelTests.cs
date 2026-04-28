// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class AssetsLayoutViewModelTests
{
    [TestMethod]
    public void NormalizeSelectedFolders_WhenRootAndConcreteFolderAreSelected_ShouldDropRoot()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        Assert.AreEqual(1, folders.Count);
        Assert.AreEqual("/Content/Materials", folders[0]);
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenConcreteFolderIsSelected_ShouldOnlyMatchThatFolder()
    {
        var folders = AssetsLayoutViewModel.NormalizeSelectedFolders(["/", "/Content/Materials"]);

        Assert.IsTrue(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat.json",
            "/Content/Materials/Red.omat.json",
            folders,
            hasActiveProject: true));
        Assert.IsFalse(AssetsLayoutViewModel.IsInSelectedFolders(
            "/Scenes/Default.oscene.json",
            "/Scenes/Default.oscene.json",
            folders,
            hasActiveProject: true));
    }
}
