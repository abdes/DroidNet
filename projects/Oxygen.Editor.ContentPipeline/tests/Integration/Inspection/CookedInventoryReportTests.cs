// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Testing;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookedInventoryReportTests
{
    /// <summary>The Inspector reports texture descriptors with their own file kind; the editor accepts every kind the index format defines.</summary>
    [TestMethod]
    public void EveryIndexedFileKindMatchesTheSchema()
    {
        // An asset descriptor is listed by its asset; only a retired record gives it this kind.
        foreach (var kind in Enum.GetValues<FileKind>().Where(static kind => kind != FileKind.AssetDescriptor))
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

    /// <summary>
    /// What a stopped writer left reads as the last commit: a mount accepts it and repair sees no damage, but a root to
    /// publish holds none of it.
    /// </summary>
    [TestMethod]
    [DataRow("uncommitted")]
    [DataRow("uncommitted_tail")]
    public void AWritersLeftoversDoNotStopARootBeingRead(string reason)
    {
        var report = CookedInventoryReport.Parse(Inventory("Resources/member.bin", FileKind.BuffersData).Replace("\"issues\":[]", $$"""
            "issues":[{"relative_path":"Resources/member.bin","reason":"{{reason}}"}]
            """, StringComparison.Ordinal));

        _ = report.IsValid.Should().BeFalse("a root to publish holds no leftovers");
        _ = report.IsMountable.Should().BeTrue();
        _ = report.ToValidation("root").Succeeded.Should().BeFalse();
        var mounted = report.ToMountValidation("root");
        _ = mounted.Succeeded.Should().BeTrue();
        _ = mounted.Diagnostics.Should().BeEmpty();
    }

    /// <summary>Damage still stops a mount, and its leftovers are left out of what it reports.</summary>
    [TestMethod]
    public void DamageBesideLeftoversStopsARootBeingRead()
    {
        var report = CookedInventoryReport.Parse(Inventory("Resources/member.bin", FileKind.BuffersData).Replace("\"issues\":[]", """
            "issues":[{"relative_path":"Resources/member.bin","reason":"digest_mismatch"},{"relative_path":"Resources/stray.bin","reason":"uncommitted"}]
            """, StringComparison.Ordinal));

        _ = report.IsMountable.Should().BeFalse();
        var mounted = report.ToMountValidation("root");
        _ = mounted.Succeeded.Should().BeFalse();
        _ = mounted.Diagnostics.Should().ContainSingle().Which.Message.Should().Contain("digest_mismatch");
    }

    private static string Inventory(string path, FileKind kind)
    {
        var sourceKey = Guid.Empty.ToString();
        var fileKind = ((int)kind).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var indexSha = new string('a', 64);
        var fileSha = new string('b', 64);
        return $$"""
            {"schema_version":{{EngineSchemaVersions.Inventory}},"source_key":"{{sourceKey}}","index_size":256,"index_sha256":"{{indexSha}}","files":[{"relative_path":"{{path}}","size":1,"sha256":"{{fileSha}}","file_kind":{{fileKind}}}],"assets":[],"resources":[],"issues":[]}
            """;
    }
}
