// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Checks observable scope changes and rejects rows from obsolete browser locations.</summary>
[TestClass]
public sealed class ContentExplorerNavigationTests
{
    /// <summary>Replacing a scope publishes one complete snapshot and never clears a self-enumerated selection.</summary>
    [TestMethod]
    public void FolderScopeChangesAreAtomicAndIdempotent()
    {
        var state = new ContentExplorerState();
        var observed = new List<AssetPath[]>();
        state.PropertyChanged += (_, _) => observed.Add([.. state.SelectedFolders]);
        state.SetSelectedFolders([AssetPath.Parse("/Content/Geometry"), AssetPath.Parse("/Content/Materials")]);
        state.SetSelectedFolders(state.SelectedFolders);
        state.SetSelectedFolders([AssetPath.Parse("/Content/Materials"), AssetPath.Parse("/Content/Geometry"), AssetPath.Parse("/Content/Materials")]);
        _ = observed.Should().ContainSingle();
        _ = observed[0].Should().BeEquivalentTo([AssetPath.Parse("/Content/Geometry"), AssetPath.Parse("/Content/Materials")]);
        state.SetSelectedFolders([AssetPath.Parse("/Content/Scenes")]);
        _ = observed.Should().HaveCount(2);
        _ = observed[1].Should().Equal(AssetPath.Parse("/Content/Scenes"));
    }

    /// <summary>Successful URLs replace every selected folder, including history and layout-switch URLs.</summary>
    [TestMethod]
    public void NavigationScopeRoundTripsMultipleFoldersAndClearsAtRoot()
    {
        var state = new ContentExplorerState();
        var folders = new[] { AssetPath.Parse("/Content/Materials"), AssetPath.Parse("/Cooked/Content/Geometry") };
        var url = "/(left:folders//right:assets/tiles)" + RouteStateMapping.BuildSelectedQuery(folders);
        RouteStateMapping.ApplyNavigationScope(state, url);
        _ = state.SelectedFolders.Should().BeEquivalentTo(folders);
        RouteStateMapping.ApplyNavigationScope(state, url: null);
        _ = state.SelectedFolders.Should().BeEquivalentTo(folders);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:folders//right:assets/list)");
        _ = state.SelectedFolders.Should().BeEmpty();
    }
}
