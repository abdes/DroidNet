// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Assets.Persistence.LooseCooked;

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

    private static string Inventory(string path, FileKind kind)
        => $$"""
            {"schema_version":1,"source_key":"{{Guid.Empty}}","index_size":256,"index_sha256":"{{new string('a', 64)}}","files":[{"relative_path":"{{path}}","size":1,"sha256":"{{new string('b', 64)}}","file_kind":{{(int)kind}}}],"assets":[],"resources":[],"issues":[]}
            """;
}
