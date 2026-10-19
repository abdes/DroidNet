// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Checks which project paths lie in the pipeline's output folders.</summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class ProjectOutputFoldersTests
{
    [TestMethod]
    [DataRow(".cooked/Content", true)]
    [DataRow(".imported/Content/Ship.ogeo", true)]
    [DataRow(@".build\cache", true)]
    [DataRow("Content/.pipeline/state.json", true)]
    [DataRow("Content/Materials/Red.omat.json", false)]
    [DataRow(".", false)]
    [DataRow("./Content", false)]
    [DataRow("SourceMedia/DCC", false)]
    public void PathInADotFolderIsOutput(string path, bool expected)
        => _ = ProjectOutputFolders.Contains(path).Should().Be(expected);
}
