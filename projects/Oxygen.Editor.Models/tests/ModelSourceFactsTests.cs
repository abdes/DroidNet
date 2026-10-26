// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Json.Schema;
using Oxygen.Editor.Models.TestSupport;

namespace Oxygen.Editor.Models.Tests;

/// <summary>
/// The facts of a model source are the <c>model</c> object of an <c>analyze-sources</c> job (model editor LLD
/// M-NGN2): its format and version, units, axes, counts, scenes and the names it produces.
/// </summary>
[TestClass]
public sealed class ModelSourceFactsTests
{
    private const string Model = """
        {
          "format": "fbx", "format_version": "7.4", "unit_meters": 0.01,
          "right": "+X", "up": "+Z", "front": "-Y", "left_handed": false,
          "node_count": 14, "mesh_count": 9,
          "scenes": [],
          "materials": [ "Paint", "Glass" ], "textures": [ "Paint_N" ]
        }
        """;

    [TestMethod]
    public void Read_TakesEveryFactTheAnalysisStates()
    {
        using var document = JsonDocument.Parse(Model);

        var facts = ModelSourceFacts.Read(document.RootElement);

        _ = facts.Format.Should().Be("fbx");
        _ = facts.SourceFormat.Should().Be(ModelSourceFormats.Fbx);
        _ = facts.FormatVersion.Should().Be("7.4");
        _ = facts.UnitMeters.Should().Be(0.01);
        _ = facts.Up.Should().Be("+Z");
        _ = facts.IsLeftHanded.Should().BeFalse();
        _ = facts.NodeCount.Should().Be(14);
        _ = facts.MeshCount.Should().Be(9);
        _ = facts.Materials.Should().Equal("Paint", "Glass");
        _ = facts.Textures.Should().Equal("Paint_N");
    }

    [TestMethod]
    public void Read_KeepsWhatTheFileDoesNotStateUnknown()
    {
        using var document = JsonDocument.Parse("""
            {
              "format": "glb", "format_version": null, "unit_meters": null,
              "right": "unknown", "up": "unknown", "front": "unknown", "left_handed": null,
              "node_count": 2, "mesh_count": 1,
              "scenes": [ { "index": 0, "name": "", "selector": "#0", "is_default": true, "root_count": 1, "node_count": 2 } ],
              "materials": [], "textures": []
            }
            """);

        var facts = ModelSourceFacts.Read(document.RootElement);

        _ = facts.SourceFormat.Should().Be(ModelSourceFormats.Gltf);
        _ = facts.FormatVersion.Should().BeNull();
        _ = facts.UnitMeters.Should().BeNull();
        _ = facts.IsLeftHanded.Should().BeNull();
        _ = facts.Scenes.Should().ContainSingle().Which.Should().Be(new ModelSourceScene(0, string.Empty, "#0", IsDefault: true, RootCount: 1, NodeCount: 2));
    }

    [TestMethod]
    public void TheFixtureIsAValidAnalysisModel()
    {
        var source = InstalledSchemas.SourceAnalysis();
        var dialect = source["$schema"]!.ToJsonString();
        var definitions = source["definitions"]!.ToJsonString();
        var schema = JsonSchema.FromText($$"""{ "$schema": {{dialect}}, "$ref": "#/definitions/model", "definitions": {{definitions}} }""");
        using var document = JsonDocument.Parse(Model);

        _ = schema.Evaluate(document.RootElement).IsValid.Should().BeTrue("the fixture is shaped as the engine reports it");
    }
}
