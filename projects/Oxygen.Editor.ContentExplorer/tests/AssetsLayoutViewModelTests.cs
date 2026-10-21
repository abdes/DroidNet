// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Tests;

[TestClass]
public sealed class AssetsLayoutViewModelTests
{
    /// <summary>A renamed derived mount filters cooked assets while an ordinary Cooked folder remains authored.</summary>
    [TestMethod]
    public void RenamedCookedMountUsesItsSavedAlias()
    {
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            [AssetPath.Parse("/Published/Content/Materials")],
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            [AssetPath.Parse("/Published/Content/Materials")],
            hasCookedProjection: false,
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeFalse();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Cooked/Red.omat",
            AssetPath.Parse("/Cooked/Red.omat"),
            [AssetPath.Parse("/Cooked")],
            cookedFolders: [AssetPath.Parse("/Published")]).Should().BeTrue();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenNoFolderIsSelected_ShouldMatchTheWholeProject()
    {
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene",
            AssetPath.Parse("/Content/Scenes/Default.oscene"),
            []).Should().BeTrue();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenConcreteFolderIsSelected_ShouldOnlyMatchThatFolder()
    {
        AssetPath[] folders = [AssetPath.Parse("/Content/Materials")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Default.oscene",
            AssetPath.Parse("/Content/Scenes/Default.oscene"),
            folders).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedRootIsSelected_ShouldMatchCookedProjectionOnly()
    {
        AssetPath[] folders = [AssetPath.Parse("/Cooked")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Uncooked.omat",
            AssetPath.Parse("/Content/Materials/Uncooked.omat"),
            folders,
            hasCookedProjection: false,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeFalse();
    }

    [TestMethod]
    public void IsInSelectedFolders_WhenCookedSubfolderIsSelected_ShouldMatchRuntimeCookedPath()
    {
        AssetPath[] folders = [AssetPath.Parse("/Cooked/Content/Materials")];

        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Materials/Red.omat",
            AssetPath.Parse("/Content/Materials/Red.omat"),
            folders,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeTrue();
        _ = AssetsLayoutViewModel.IsInSelectedFolders(
            "/Content/Scenes/Main.oscene",
            AssetPath.Parse("/Content/Scenes/Main.oscene"),
            folders,
            hasCookedProjection: true,
            cookedFolders: [AssetPath.Parse("/Cooked")]).Should().BeFalse();
    }
}
