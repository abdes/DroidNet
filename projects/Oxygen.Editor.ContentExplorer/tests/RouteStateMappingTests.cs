// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Routing;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>Preserves selected folder sets through the production router's query serialization.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed class RouteStateMappingTests
{
    /// <summary>Navigation completion reads the same multi-folder scope recognized during activation.</summary>
    [TestMethod]
    public void MultipleFoldersSurviveRouterNormalization()
    {
        AssetPath[] folders = [AssetPath.Parse("/Cooked/Content/Models/Crate/Geometry"), AssetPath.Parse("/Cooked/Content/Models/Crate/Materials")];
        var url = "/(left:folders//right:assets/tiles)" + RouteStateMapping.BuildSelectedQuery(folders);
        var serialized = DefaultUrlSerializer.Instance.Serialize(DefaultUrlSerializer.Instance.Parse(url));
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl(url).Should().Equal(folders);
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl(serialized).Should().Equal(folders);
    }

    /// <summary>The query carries escaped virtual paths; a value that is not one selects nothing.</summary>
    [TestMethod]
    public void QueryCarriesEscapedVirtualPathsAndIgnoresOtherValues()
    {
        var folder = AssetPath.Parse("/Content/Materials");

        _ = RouteStateMapping.BuildSelectedQuery([folder]).Should().Be("?selected=%2FContent%2FMaterials");
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl("/(left:folders//right:assets/tiles)?selected=%2FContent%2FMaterials").Should().Equal(folder);
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl("/(left:folders//right:assets/tiles)?selected=Content%2FMaterials").Should().BeEmpty();
        _ = RouteStateMapping.BuildSelectedQuery([]).Should().BeEmpty("no folder selected browses the whole project");
    }
}
