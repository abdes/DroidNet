// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class LibraryMetadataCacheTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A partially written or manually damaged cache must not break asset-status reads.</summary>
    /// <param name="missingReport">Whether the cached report is missing instead of malformed JSON.</param>
    /// <returns>The asynchronous cache-corruption check.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task InvalidLibraryMetadataIsACacheMiss(bool missingReport)
    {
        using var workspace = new CookWorkspace();
        var fingerprint = new string('A', 64);
        var content = missingReport ? JsonSerializer.Serialize(new { Fingerprint = fingerprint, Digest = "invalid", Report = (string?)null }) : "{";
        workspace.WriteText(".build/cache/cooked-dependencies-v2/" + fingerprint + ".json", content);
        _ = (await CookedDependencyCache.ReadAsync(workspace.Root, fingerprint, [], this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull();
    }
}
