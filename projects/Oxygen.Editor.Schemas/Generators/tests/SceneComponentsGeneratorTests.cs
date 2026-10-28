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
        version: 2
        units:
          - { id: metre, abbreviation: m, name: metres }
          - { id: degree, abbreviation: "°", name: degrees }
          - { id: radian, abbreviation: rad, name: radians }
        enums:
          - name: CameraAspectMode
            members:
              - { name: Auto, value: 0 }
              - { name: Fixed, value: 1 }
        field_sets:
          - name: Lamp
            fields:
              - { name: Range, id: 5 }
              - { name: ColorR, id: 0 }
              - { name: ColorG, id: 1 }
              - { name: ColorB, id: 2 }
              - { name: FovRadians, id: 3 }
              - { name: Mode, id: 4 }
        components:
          - name: Lamp
            kind: lamp
            cardinality: optional
            wire: { id: 9, field_set: Lamp }
            properties:
              - name: Color
                key: /color
                type: vec3
                fields: [ColorR, ColorG, ColorB]
                override_id: 3
                default: [1, 1, 1]
                range: { min: 0, max: 1 }
                commit: clamp
              - name: Range
                key: /range
                type: float
                fields: [Range]
                override_id: 4
                default: 20
                range: { min: 0, min_exclusive: true }
                unit: metre
              - name: FovDegrees
                key: /fov_degrees
                type: float
                fields: [FovRadians]
                encoding: degrees-to-radians
                override_id: 5
                default: 60
                range: { min: 1, max: 179 }
                commit: clamp
                unit: degree
              - name: Mode
                key: /mode
                type: enum
                enum: CameraAspectMode
                fields: [Mode]
                override_id: 6
                default: Auto
          - name: Geometry
            kind: geometry
            cardinality: required
            properties:
              - name: GeometryUri
                key: /geometry_uri
                type: asset
              - name: LodPolicy
                key: /lod_policy
                type: lod-policy
                sources: [asset]
                override_id: 2
                default: { type: distance, thresholds: [10, 20], hysteresis_ratio: 0.1 }
            parts:
              - name: Surface
                kind: surface
                properties:
                  - name: Material
                    key: /material
                    type: asset
                    sources: [asset]
                    override_id: 1
        """;

    [TestMethod]
    public async Task Generate_EngineSchema()
        => await this.Check(
            AllOutputs,
            await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Schema", "SceneComponents.yaml"), this.TestContext.CancellationToken).ConfigureAwait(false))
            .ConfigureAwait(false);

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
        => this.Check(AllOutputs, Minimal.Replace("key: /geometry_uri\n        type: asset", "key: /geometry_uri\n        type: float\n        fields: [Range]\n        override_id: 7", StringComparison.Ordinal));

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
        => this.Check(AllOutputs, Minimal.Replace("            sources: [asset]\n            override_id: 1", "            sources: [asset]\n            fields: [Range]\n            override_id: 1", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownSource()
        => this.Check(AllOutputs, Minimal.Replace("sources: [asset]\n        override_id: 2", "sources: [asset, prefab]\n        override_id: 2", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_PartNamedLikeProperty()
        => this.Check(AllOutputs, Minimal.Replace("- name: Surface", "- name: GeometryUri", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_MissingDefault()
        => this.Check(AllOutputs, Minimal.Replace("        default: 20\n", string.Empty, StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_DefaultOutsideRange()
        => this.Check(AllOutputs, Minimal.Replace("default: 20", "default: 0", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_RangeOnBool()
        => this.Check(AllOutputs, Minimal.Replace("type: float\n        fields: [Range]\n        override_id: 4\n        default: 20", "type: bool\n        fields: [Range]\n        override_id: 4\n        default: true", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_UnknownUnit()
        => this.Check(AllOutputs, Minimal.Replace("unit: metre", "unit: foot", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_DisplayUnitOfNonRadian()
        => this.Check(AllOutputs, Minimal.Replace("unit: metre", "unit: metre\n        display_unit: degree", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_EnumDefaultNotAMember()
        => this.Check(AllOutputs, Minimal.Replace("default: Auto", "default: Stretch", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_OverrideIdWithoutSources()
        => this.Check(AllOutputs, Minimal.Replace("type: asset\n      - name: LodPolicy", "type: asset\n        override_id: 9\n      - name: LodPolicy", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_WiredPropertyWithoutOverrideId()
        => this.Check(AllOutputs, Minimal.Replace("        override_id: 4\n", string.Empty, StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_RotationEncoding()
        => this.Check("WireTable", Minimal.Replace("fields: [ColorR, ColorG, ColorB]", "fields: [ColorR, ColorG, ColorB]\n        encoding: euler-xyz-degrees-to-quat", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_DuplicateOverrideId()
        => this.Check(AllOutputs, Minimal.Replace("override_id: 2", "override_id: 1", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_LodPolicyWithTooManyBoundaries()
        => this.Check(AllOutputs, Minimal.Replace("thresholds: [10, 20]", "thresholds: [1, 2, 3, 4, 5, 6, 7, 8]", StringComparison.Ordinal));

    [TestMethod]
    public Task Generate_EnumMatchingItsEditorType()
        => this.Check("PropertyIds", Minimal, "namespace Oxygen.Managed.Core { public enum CameraAspectMode { Auto = 0, Fixed = 1 } }");

    [TestMethod]
    public Task Generate_EnumDifferingFromItsEditorType()
        => this.Check("PropertyIds", Minimal, "namespace Oxygen.Managed.Core { public enum CameraAspectMode { Auto = 0, Fixed = 2 } }");

    private Task Check(string outputs, string? schema, string? code = null)
    {
        var compilation = CSharpCompilation.Create(
            "Tests",
            code is null ? [] : [CSharpSyntaxTree.ParseText(code, cancellationToken: this.TestContext.CancellationToken)],
            Basic.Reference.Assemblies.Net80.References.All,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
        AdditionalText[] files = schema is null ? [] : [new InMemoryText("Schema/SceneComponents.yaml", schema.Replace("\r\n", "\n", StringComparison.Ordinal))];
        GeneratorDriver driver = CSharpGeneratorDriver.Create(
            [new SceneComponentsGenerator().AsSourceGenerator()],
            files,
            optionsProvider: new Options(outputs));
        return this.Verify(driver.RunGenerators(compilation, this.TestContext.CancellationToken)).UseDirectory("Snapshots");
    }

    private sealed class InMemoryText(string path, string text) : AdditionalText
    {
        public override string Path { get; } = path;

        public override SourceText GetText(CancellationToken cancellationToken = default) => SourceText.From(text);
    }

    private sealed class Options(string outputs) : AnalyzerConfigOptionsProvider
    {
        public override AnalyzerConfigOptions GlobalOptions { get; } = new Values(outputs);

        public override AnalyzerConfigOptions GetOptions(SyntaxTree tree) => new Values(outputs: null);

        public override AnalyzerConfigOptions GetOptions(AdditionalText textFile) => new Values(outputs: null);
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
