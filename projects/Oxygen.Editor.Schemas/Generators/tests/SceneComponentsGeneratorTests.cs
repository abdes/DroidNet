// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Text;

namespace Oxygen.Editor.Schemas.Generators.Tests;

/// <summary>
/// Snapshots the code generated from the scene components schema and the errors of a bad schema: each snapshot holds
/// the generated files and the diagnostics with their locations.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class SceneComponentsGeneratorTests : VerifyBase
{
    private const string AllOutputs = "WireIds;PropertyIds;WireTable";

    private const string Minimal = """
        version: 1
        field_sets:
          - name: Lamp
            fields:
              - { name: Range, id: 5 }
              - { name: ColorR, id: 0 }
              - { name: ColorG, id: 1 }
              - { name: ColorB, id: 2 }
              - { name: FovRadians, id: 3 }
        components:
          - name: Lamp
            kind: lamp
            wire: { id: 9, field_set: Lamp }
            properties:
              - name: Color
                key: /color
                type: vec3
                fields: [ColorR, ColorG, ColorB]
              - name: Range
                key: /range
                type: float
                fields: [Range]
              - name: FovDegrees
                key: /fov_degrees
                type: float
                fields: [FovRadians]
                encoding: degrees-to-radians
          - name: Geometry
            kind: geometry
            properties:
              - name: GeometryUri
                key: /geometry_uri
                type: asset
            parts:
              - name: Surface
                kind: surface
                properties:
                  - name: Material
                    key: /material
                    type: asset
                    sources: [asset]
        """;

    [TestMethod]
    public Task Generate_EngineSchema()
        => this.Check(AllOutputs, File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Schema", "SceneComponents.yaml")));

    [TestMethod]
    public Task Generate_EveryOutput() => this.Check(AllOutputs, Minimal);

    [TestMethod]
    public Task Generate_OneOutput() => this.Check("PropertyIds", Minimal);

    [TestMethod]
    public Task Generate_UnknownOutput() => this.Check("WireIds;Everything", Minimal);

    [TestMethod]
    public Task Generate_NoOutput() => this.Check(string.Empty, Minimal);

    [TestMethod]
    public Task Generate_NoSchema() => this.Check(AllOutputs, schema: null);

    [TestMethod]
    public Task Generate_MalformedYaml()
        => this.Check(AllOutputs, Minimal.Replace("- { name: Range, id: 5 }", "- { name: Range,, id: 5 }", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownKey()
        => this.Check(AllOutputs, Minimal.Replace("    kind: lamp\n", "    kind: lamp\n    colour: red\n", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_MissingKeyAndWrongValue()
        => this.Check(AllOutputs, Minimal.Replace("        key: /range\n", string.Empty, StringComparison.Ordinal).Replace("id: 9", "id: nine", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownField()
        => this.Check(AllOutputs, Minimal.Replace("fields: [Range]", "fields: [Reach]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownFieldSet()
        => this.Check(AllOutputs, Minimal.Replace("field_set: Lamp", "field_set: Torch", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_EnumWithoutEditorType()
        => this.Check(AllOutputs, Minimal.Replace("type: float\n        fields: [Range]", "type: enum\n        enum: LampMood\n        fields: [Range]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_VectorWithOneField()
        => this.Check(AllOutputs, Minimal.Replace("fields: [ColorR, ColorG, ColorB]", "fields: [ColorR]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_FieldsWithoutWire()
        => this.Check(AllOutputs, Minimal.Replace("key: /geometry_uri\n        type: asset", "key: /geometry_uri\n        type: float\n        fields: [Range]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownEncoding()
        => this.Check(AllOutputs, Minimal.Replace("degrees-to-radians", "turns-to-radians", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_Duplicates()
        => this.Check(AllOutputs, Minimal.Replace("{ name: FovRadians, id: 3 }", "{ name: FovRadians, id: 5 }", StringComparison.Ordinal).Replace("  - name: Geometry\n", "  - name: Lamp\n", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_PartPropertyWithoutSource()
        => this.Check(AllOutputs, Minimal.Replace("\n            sources: [asset]", string.Empty, StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_PartPropertyWithFields()
        => this.Check(AllOutputs, Minimal.Replace("sources: [asset]", "sources: [asset]\n            fields: [Range]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownSource()
        => this.Check(AllOutputs, Minimal.Replace("sources: [asset]", "sources: [asset, prefab]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_PartNamedLikeProperty()
        => this.Check(AllOutputs, Minimal.Replace("- name: Surface", "- name: GeometryUri", StringComparison.Ordinal));

    private Task Check(string outputs, string? schema)
    {
        var compilation = CSharpCompilation.Create(
            "Tests",
            [],
            Basic.Reference.Assemblies.Net80.References.All,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        AdditionalText[] files = schema is null ? [] : [new InMemoryText("Schema/SceneComponents.yaml", schema.Replace("\r\n", "\n", StringComparison.Ordinal))];
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            [new SceneComponentsGenerator().AsSourceGenerator()],
            files,
            optionsProvider: new Options(outputs));
        return this.Verify(driver.RunGenerators(compilation)).UseDirectory("Snapshots");
    }

    private sealed class InMemoryText(string path, string text) : AdditionalText
    {
        public override string Path { get; } = path;

        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(text);
    }

    private sealed class Options(string outputs) : AnalyzerConfigOptionsProvider
    {
        public override AnalyzerConfigOptions GlobalOptions { get; } = new Values(outputs);

        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => new Values(null);

        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => new Values(null);
    }

    private sealed class Values(string? outputs) : AnalyzerConfigOptions
    {
        public override bool TryGetValue(string key, [NotNullWhen(true)] out string? value)
        {
            value = string.Equals(key, "build_property.SceneComponentsOutput", StringComparison.Ordinal) ? outputs : null;
            return value is not null;
        }
    }
}
