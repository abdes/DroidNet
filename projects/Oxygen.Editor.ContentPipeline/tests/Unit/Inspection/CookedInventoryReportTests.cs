// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Testing;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookedInventoryReportTests
{
    /// <summary>The Inspector reports texture descriptors with their own file kind; the editor accepts every kind the index format defines.</summary>
    [TestMethod]
    public void EveryIndexedFileKindMatchesTheSchema()
    {
        foreach (var kind in Enum.GetValues<FileKind>())
        {
            var report = CookedInventoryReport.Parse(Inventory("Resources/member.bin", kind));

            _ = report.Files["Resources/member.bin"].Kind.Should().Be(kind);
        }
    }

    /// <summary>
    /// A descriptor in an asset format the engine no longer reads is excluded from the valid set on its own: the shared
    /// files and the other descriptors stay valid, so only the owning source is rebuilt.
    /// </summary>
    [TestMethod]
    public void AFormatVersionIssueExcludesOnlyItsDescriptor()
    {
        var root = Directory.CreateTempSubdirectory("oxygen-format-version-").FullName;
        try
        {
            Directory.CreateDirectory(Path.Combine(root, "Geometry"));
            File.WriteAllText(Path.Combine(root, "Geometry", "Old.ogeo"), "old format");
            File.WriteAllText(Path.Combine(root, "Geometry", "Current.ogeo"), "current format");
            File.WriteAllText(Path.Combine(root, "shared.bin"), "shared");
            NativeInventoryFixture.WriteIndex(
                root,
                [
                    new CookedAssetEntry("/Content/Geometry/Old.ogeo", ContentCookAssetKind.Geometry) { DescriptorRelativePath = "Geometry/Old.ogeo" },
                    new CookedAssetEntry("/Content/Geometry/Current.ogeo", ContentCookAssetKind.Geometry) { DescriptorRelativePath = "Geometry/Current.ogeo" },
                ]);
            var json = JsonNode.Parse(NativeInventoryFixture.ReadJson(root))!;
            json["issues"]!.AsArray().Add(new JsonObject { ["relative_path"] = "Geometry/Old.ogeo", ["reason"] = "format_version" });
            var inventory = CookedInventoryReport.Parse(json.ToJsonString());
            var provenanceRoot = new CookProvenance.Root("Content", inventory.SourceKey, inventory.IndexSha256);

            var (sharedFilesValid, validDescriptors) = provenanceRoot.Compare(inventory, []);

            _ = sharedFilesValid.Should().BeTrue();
            _ = validDescriptors.Should().Contain("Geometry/Current.ogeo").And.NotContain("Geometry/Old.ogeo");
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    private static string Inventory(string path, FileKind kind)
        => $$"""
            {"schema_version":1,"source_key":"{{Guid.Empty}}","index_size":256,"index_sha256":"{{new string('a', 64)}}","files":[{"relative_path":"{{path}}","size":1,"sha256":"{{new string('b', 64)}}","file_kind":{{(int)kind}}}],"assets":[],"resources":[],"issues":[]}
            """;
}
