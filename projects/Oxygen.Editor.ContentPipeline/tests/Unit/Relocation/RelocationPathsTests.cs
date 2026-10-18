// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>Checks the path mapping relocation builds on: renames in NFC and case-sensitive identities.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class RelocationPathsTests
{
    /// <summary>A typed name is trimmed and put in NFC; the extension is kept.</summary>
    [TestMethod]
    public void RenameTrimsTheTypedNameAndPutsItInNfc()
    {
        var renamed = RelocationPaths.Rename("/Content/Materials/Red.omat.json", " Cafe\u0301 ", isFolder: false);

        _ = renamed.Should().Be("/Content/Materials/Caf\u00E9.omat.json");
    }

    /// <summary>Identity is case-sensitive: a folder differing only in case does not contain a path.</summary>
    [TestMethod]
    public void FolderDifferingOnlyInCaseDoesNotContainAPath()
    {
        _ = RelocationPaths.IsSameOrInside("/Content/Materials/Red.omat", "/Content/materials").Should().BeFalse();
        _ = RelocationPaths.IsSameOrInside("/Content/Materials/Red.omat", "/Content/Materials").Should().BeTrue();
    }
}
