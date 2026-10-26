// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// Before import shows each finding of a file once and names the objects it is about, instead of one item per object
/// the engine reports it for.
/// </summary>
[TestClass]
public sealed class BeforeImportItemTests
{
    private const string Dropped = "import.source_feature_dropped";
    private const string Sampling = "The material's texture wrap and filter modes are not imported; its textures repeat and use trilinear filtering.";

    [TestMethod]
    public void From_GroupsOneFindingOfManyObjectsIntoOneItemThatNamesThem()
    {
        string[] materials = ["Bun", "Cheese", "Sauce", "Lettuce", "Patty"];

        var items = BeforeImportItem.From([.. materials.Select((name, index) => Issue(Sampling, $"/materials/{index}", name)), Issue("Animations are not imported; the model imports in its rest pose.", "/animations", name: null)]);

        _ = items.Should().HaveCount(2);
        _ = items[0].Objects.Should().Equal(materials);
        _ = items[1].Objects.Should().BeEmpty("a finding about the whole file names no object");
    }

    [TestMethod]
    public void From_NamesAnUnnamedObjectByItsKindAndIndex()
    {
        var items = BeforeImportItem.From([Issue(Sampling, "/materials/3", name: null), Issue(Sampling, "/Materials/4", name: null)]);

        _ = items.Should().ContainSingle().Which.Objects.Should().Equal("material #3", "material #4");
    }

    [TestMethod]
    public void From_KeepsDifferentMessagesApartAndOneObjectOnce()
    {
        var items = BeforeImportItem.From(
        [
            Issue(Sampling, "/materials/0", "Bun"),
            Issue(Sampling, "/materials/0", "Bun"),
            Issue("The material's specular colour is not imported; its specular reflection is white.", "/materials/0", "Bun"),
        ]);

        _ = items.Should().HaveCount(2);
        _ = items[0].Objects.Should().Equal("Bun");
    }

    private static DiagnosticRecord Issue(string message, string path, string? name)
        => new()
        {
            OperationId = Guid.Empty,
            Domain = FailureDomain.AssetImport,
            Severity = DiagnosticSeverity.Warning,
            Code = Dropped,
            Message = message,
            AffectedEntity = new() { SourceObjectPath = path, SourceObjectName = name },
        };
}
