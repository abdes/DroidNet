// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Checks observable scope changes and rejects rows from obsolete browser locations.</summary>
[TestClass]
public sealed class ContentBrowserNavigationTests
{
    /// <summary>Replacing a scope publishes one complete snapshot and never clears a self-enumerated selection.</summary>
    [TestMethod]
    public void FolderScopeChangesAreAtomicAndIdempotent()
    {
        var state = new ContentBrowserState(new ProjectContextService());
        var observed = new List<string[]>();
        state.PropertyChanged += (_, _) => observed.Add([.. state.SelectedFolders]);
        state.SetSelectedFolders(["/Content/Geometry", "/Content/Materials"]);
        state.SetSelectedFolders(state.SelectedFolders);
        state.SetSelectedFolders(["/Content/Materials", "/Content/Geometry", "/Content/Materials"]);
        _ = observed.Should().ContainSingle();
        _ = observed[0].Should().BeEquivalentTo(["/Content/Geometry", "/Content/Materials"]);
        state.SetSelectedFolders(["/Content/Scenes"]);
        _ = observed.Should().HaveCount(2);
        _ = observed[1].Should().Equal("/Content/Scenes");
    }

    /// <summary>Successful URLs replace every selected folder, including history and layout-switch URLs.</summary>
    [TestMethod]
    public void NavigationScopeRoundTripsMultipleFoldersAndClearsAtRoot()
    {
        var state = new ContentBrowserState(new ProjectContextService());
        var folders = new[] { "/Content/Materials", "/Cooked/Content/Geometry" };
        var url = "/(left:project//right:assets/tiles)" + RouteStateMapping.BuildSelectedQuery(folders);
        RouteStateMapping.ApplyNavigationScope(state, url);
        _ = state.SelectedFolders.Should().BeEquivalentTo(folders);
        RouteStateMapping.ApplyNavigationScope(state, url: null);
        _ = state.SelectedFolders.Should().BeEquivalentTo(folders);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)");
        _ = state.SelectedFolders.Should().BeEmpty();
    }

    /// <summary>Legacy root selection has the same scope as the canonical virtual root.</summary>
    [TestMethod]
    public void LegacyRootDoesNotHideTheProjectOrBroadenAConcreteFolder()
    {
        _ = AssetsLayoutViewModel.NormalizeSelectedFolders(["."]).Should().Equal("/");
        _ = AssetsLayoutViewModel.NormalizeSelectedFolders([".", "/Content/Materials"]).Should().Equal("/Content/Materials");
    }
}
