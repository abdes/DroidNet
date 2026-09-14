// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks that corrupt derived metadata becomes a cache miss.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>A partially written or manually damaged cache must not break asset-status reads.</summary>
    /// <param name="missingReport">Whether the cached report is missing instead of malformed JSON.</param>
    /// <returns>The asynchronous cache-corruption check.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task InvalidLibraryMetadataIsACacheMiss(bool missingReport)
    {
        using var workspace = new TempWorkspace();
        var fingerprint = new string('A', 64);
        var content = missingReport ? JsonSerializer.Serialize(new { Fingerprint = fingerprint, Digest = "invalid", Report = (string?)null }) : "{";
        workspace.WriteText(".build/cache/cooked-dependencies-v1/" + fingerprint + ".json", content);
        _ = (await CookedDependencyCache.ReadAsync(workspace.Root, fingerprint, [], this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull();
    }
}
