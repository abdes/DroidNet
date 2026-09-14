// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Routing;
using Oxygen.Editor.ContentBrowser.Shell;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Preserves selected folder sets through the production router's query serialization.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed class RouteStateMappingTests
{
    /// <summary>Navigation completion reads the same multi-folder scope recognized during activation.</summary>
    [TestMethod]
    public void MultipleFoldersSurviveRouterNormalization()
    {
        string[] folders = ["/Cooked/Content/Models/Crate/Geometry", "/Cooked/Content/Models/Crate/Materials"];
        var url = "/(left:project//right:assets/tiles)" + RouteStateMapping.BuildSelectedQuery(folders);
        var serialized = DefaultUrlSerializer.Instance.Serialize(DefaultUrlSerializer.Instance.Parse(url));
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl(url).Should().Equal(folders);
        _ = RouteStateMapping.ParseSelectedFoldersFromUrl(serialized).Should().Equal(folders);
    }
}
