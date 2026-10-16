// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>Checks that the editor accepts exactly the names the engine can cook.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class RelocationPathsTests
{
    /// <summary>Names made of the engine's segment characters are accepted.</summary>
    /// <param name="name">The name.</param>
    [TestMethod]
    [DataRow("New folder")]
    [DataRow("New folder (2)")]
    [DataRow("Wet Rubber (2).omat.json")]
    [DataRow("Rock_01-b")]
    public void EngineCookableNameIsAccepted(string name)
        => _ = RelocationPaths.ValidateName(name).Should().BeNull();

    /// <summary>Names the engine cannot cook, or the file system cannot keep, are rejected.</summary>
    /// <param name="name">The name.</param>
    [TestMethod]
    [DataRow("Rock&Roll")]
    [DataRow("Rubber[2]")]
    [DataRow("Café")]
    [DataRow(" Leading")]
    [DataRow("Trailing ")]
    [DataRow(".hidden")]
    public void NameTheEngineCannotCookIsRejected(string name)
        => _ = RelocationPaths.ValidateName(name).Should().NotBeNull();
}
