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
        workspace.WriteText(".build/cache/cooked-dependencies-v3/" + fingerprint + ".json", content);
        _ = (await CookedDependencyCache.ReadAsync(workspace.Root, fingerprint, [], this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull();
    }

    /// <summary>A library's cooked textures have no dependency entries; a report of its assets still matches it.</summary>
    /// <returns>The asynchronous texture-record check.</returns>
    [TestMethod]
    public async Task ReportMatchesALibraryThatAlsoHoldsTextures()
    {
        // Arrange
        using var workspace = new CookWorkspace();
        var fingerprint = new string('B', 64);
        var source = Guid.Parse("aaaaaaaa-aaaa-7aaa-8aaa-aaaaaaaaaaaa");
        var material = Guid.Parse("11111111-1111-4111-8111-111111111111");
        var report = $$"""
            { "schema": "oxygen.cooked-dependencies.v3", "source_key": "{{source}}", "assets": [
              { "asset_key": "{{material}}", "asset_type": 1, "virtual_path": "/Art/Materials/Tile.omat",
                "dependencies": [], "key_references": [], "resource_bindings": [], "complete": true } ] }
            """;
        var digest = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(report)));
        workspace.WriteText(".build/cache/cooked-dependencies-v3/" + fingerprint + ".json", JsonSerializer.Serialize(new { Fingerprint = fingerprint, Digest = digest, Report = report }));
        var root = Oxygen.Managed.Core.FileSystemPath.From(workspace.Root);
        var tile = Oxygen.Managed.Core.AssetPath.Parse("/Art/Materials/Tile.omat");
        var grid = Oxygen.Managed.Core.AssetPath.Parse("/Art/Textures/Grid.otex");
        Oxygen.Managed.Assets.Catalog.AssetRecord[] records =
        [
            new(tile) { Cooked = new(root, "Materials/Tile.omat", source, Oxygen.Managed.Assets.Persistence.AssetKey.FromBytes(material.ToByteArray(bigEndian: true)), 1, 1, new string('0', 64)) { VirtualPath = tile } },
            new(grid) { Cooked = new(root, "Textures/Grid.otex", source, Oxygen.Managed.Assets.Persistence.AssetKey.FromVirtualPath(grid.VirtualPath), 0, 51, new string('0', 64)) { VirtualPath = grid } },
        ];

        // Act
        var cached = await CookedDependencyCache.ReadAsync(workspace.Root, fingerprint, records, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = cached.Should().NotBeNull();
    }
}
