// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class CookedDependencyReportTests
{
    [TestMethod]
    public void Parse_UsesOnlyAssetTargetReferencesAsDependencies()
    {
        var report = CookedDependencyReport.Parse(ReportJson);
        var asset = report.Assets["11111111-1111-4111-8111-111111111111"];

        _ = asset.Dependencies.Should().ContainSingle().Which.Should().Be("22222222-2222-4222-8222-222222222222");
        _ = asset.KeyReferences.Select(static reference => reference.TargetKind)
            .Should().Equal(CookedKeyReferenceTargetKind.Asset, CookedKeyReferenceTargetKind.PhysicsResource, CookedKeyReferenceTargetKind.Logical);
        _ = asset.KeyReferences[0].ExpectedAssetType.Should().Be(2);
        _ = asset.ResourceBindings.Select(static binding => binding.State)
            .Should().Equal(CookedResourceBindingState.Fallback, CookedResourceBindingState.Error);
    }

    [TestMethod]
    public void Parse_RejectsDependencyListThatIncludesNonAssetReferences()
    {
        var malformed = ReportJson.Replace(
            "\"dependencies\": [\"22222222-2222-4222-8222-222222222222\"]",
            "\"dependencies\": [\"22222222-2222-4222-8222-222222222222\", \"33333333-3333-4333-8333-333333333333\"]",
            StringComparison.Ordinal);

        var parse = () => CookedDependencyReport.Parse(malformed);
        _ = parse.Should().Throw<InvalidDataException>().WithMessage("*do not match its asset-target key references*");
    }

    [TestMethod]
    public void Parse_RejectsLegacyWireSchemaIdentifier()
    {
        var legacy = ReportJson.Replace("oxygen.cooked-dependencies.v2", "oxygen.cooked-dependencies.v1", StringComparison.Ordinal);

        var parse = () => CookedDependencyReport.Parse(legacy);
        _ = parse.Should().Throw<InvalidDataException>()
            .WithMessage("*Expected oxygen.cooked-dependencies.v2; received 'oxygen.cooked-dependencies.v1'*installed native Inspector*");
    }

    [TestMethod]
    [DataRow((byte)2, true)]
    [DataRow((byte)3, false)]
    public void AssetTargetReferenceRequiresMatchingIndexedType(byte indexedType, bool expectedMatch)
    {
        var reference = new CookedKeyReference("22222222-2222-4222-8222-222222222222", CookedKeyReferenceTargetKind.Asset, 2);
        var target = new Oxygen.Managed.Assets.Catalog.CookedAssetMetadata(
            "C:\\Cooked", "assets/target.bin", Guid.CreateVersion7(), new(1, 2), indexedType, 0, new string('0', 64));

        _ = global::Oxygen.Editor.ContentPipeline.Publication.CookedLibraryReadSet.MatchesExpectedAssetType(reference, target).Should().Be(expectedMatch);
    }

    private const string ReportJson = """
        {
          "schema": "oxygen.cooked-dependencies.v2",
          "source_key": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
          "assets": [
            {
              "asset_key": "11111111-1111-4111-8111-111111111111",
              "asset_type": 2,
              "virtual_path": "/Content/Materials/Test.omat",
              "dependencies": ["22222222-2222-4222-8222-222222222222"],
              "key_references": [
                { "asset_key": "22222222-2222-4222-8222-222222222222", "target_kind": 1, "expected_asset_type": 2 },
                { "asset_key": "33333333-3333-4333-8333-333333333333", "target_kind": 2, "expected_asset_type": 0 },
                { "asset_key": "44444444-4444-4444-8444-444444444444", "target_kind": 3, "expected_asset_type": 0 }
              ],
              "resource_bindings": [
                { "kind": 2, "index": 0, "state": "fallback" },
                { "kind": 2, "index": 4294967295, "state": "error" }
              ],
              "complete": true
            }
          ]
        }
        """;
}
