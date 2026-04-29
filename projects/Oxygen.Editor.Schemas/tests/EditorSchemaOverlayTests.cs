// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using AwesomeAssertions;

namespace Oxygen.Editor.Schemas.Tests;

[TestClass]
public sealed class EditorSchemaOverlayTests
{
    [TestMethod]
    public void LintAnnotationNamespace_WhenOverlayAddsValidationKeyword_ShouldReportViolation()
    {
        var overlay = JsonNode.Parse(
            """
            {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "$ref": "oxygen.material-descriptor.schema.json",
              "properties": {
                "parameters": {
                  "properties": {
                    "metalness": {
                      "x-editor-label": "Metalness",
                      "x-editor-renderer": "slider",
                      "minimum": 0.25
                    }
                  }
                }
              }
            }
            """)!.AsObject();

        var violations = EditorSchemaOverlay.LintAnnotationNamespace(overlay);

        _ = violations.Should().ContainSingle()
            .Which.Should().Be(("/parameters/metalness", "minimum"));
    }

    [TestMethod]
    public void ExtractAnnotations_ShouldIndexAnnotationsByJsonPointer()
    {
        var overlay = JsonNode.Parse(
            """
            {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "$ref": "oxygen.material-descriptor.schema.json",
              "properties": {
                "parameters": {
                  "x-editor-label": "Parameters",
                  "x-editor-renderer": "section",
                  "properties": {
                    "metalness": {
                      "x-editor-label": "Metalness",
                      "x-editor-group": "Surface",
                      "x-editor-order": 10,
                      "x-editor-renderer": "slider",
                      "x-editor-step": 0.01,
                      "x-editor-soft-max": 1.0,
                      "x-editor-color-space": "linear"
                    }
                  }
                }
              }
            }
            """)!.AsObject();

        var annotations = EditorSchemaOverlay.ExtractAnnotations(overlay);

        _ = annotations.Should().ContainKey("/parameters/metalness");
        var metalness = annotations["/parameters/metalness"];
        _ = metalness.Label.Should().Be("Metalness");
        _ = metalness.Group.Should().Be("Surface");
        _ = metalness.Order.Should().Be(10);
        _ = metalness.Renderer.Should().Be("slider");
        _ = metalness.Step.Should().Be(0.01);
        _ = metalness.SoftMax.Should().Be(1.0);
        _ = metalness.Extra.Should().Contain("x-editor-color-space", "linear");
    }

    [TestMethod]
    public void FindMissingAnnotationCoverage_WhenOverlayOmitsLeafWidget_ShouldReportPath()
    {
        var engine = JsonNode.Parse(
            """
            {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "type": "object",
              "properties": {
                "parameters": {
                  "$ref": "#/definitions/parameters"
                }
              },
              "definitions": {
                "parameters": {
                  "type": "object",
                  "properties": {
                    "metalness": { "type": "number" },
                    "roughness": { "type": "number" }
                  }
                }
              }
            }
            """)!.AsObject();
        var overlay = JsonNode.Parse(
            """
            {
              "$schema": "http://json-schema.org/draft-07/schema#",
              "$ref": "oxygen.material-descriptor.schema.json",
              "properties": {
                "parameters": {
                  "x-editor-label": "Parameters",
                  "x-editor-renderer": "section",
                  "properties": {
                    "metalness": {
                      "x-editor-label": "Metalness",
                      "x-editor-renderer": "slider"
                    }
                  }
                }
              }
            }
            """)!.AsObject();

        var missing = EditorSchemaOverlay.FindMissingAnnotationCoverage(engine, overlay);

        _ = missing.Should().Equal("/parameters/roughness");
    }

    [TestMethod]
    public void TransformOverlay_ShouldCoverAllAuthorableEngineFields()
    {
        var schemaRoot = FindSchemaRoot();
        var engine = LoadSchema(schemaRoot, "oxygen.transform-component.schema.json");
        var overlay = LoadSchema(schemaRoot, "oxygen.transform-component.editor.schema.json");

        var missing = EditorSchemaOverlay.FindMissingAnnotationCoverage(
            engine,
            overlay,
            hiddenPaths: ["/kind"]);

        _ = missing.Should().BeEmpty();
    }

    [TestMethod]
    public void MaterialOverlay_ShouldCoverAllAuthorableEngineFields()
    {
        var schemaRoot = FindSchemaRoot();
        var engine = LoadSchema(schemaRoot, "oxygen.material-descriptor.schema.json");
        var overlay = LoadSchema(schemaRoot, "oxygen.material-descriptor.editor.schema.json");

        var missing = EditorSchemaOverlay.FindMissingAnnotationCoverage(
            engine,
            overlay,
            hiddenPaths: ["/$schema"]);

        _ = missing.Should().BeEmpty();
    }

    [TestMethod]
    public void CookerEmbeddedSchemas_ShouldNotIncludeEditorOverlays()
    {
        var repoRoot = FindRepoRoot();
        var cmakeFiles = new[]
        {
            Path.Combine(repoRoot, "projects", "Oxygen.Engine", "src", "Oxygen", "Cooker", "CMakeLists.txt"),
            Path.Combine(repoRoot, "projects", "Oxygen.Engine", "src", "Oxygen", "Cooker", "Tools", "PakTool", "CMakeLists.txt"),
        };

        foreach (var cmakeFile in cmakeFiles)
        {
            var contents = File.ReadAllText(cmakeFile);
            var embeddedSchemaBlocks = new Regex(
                @"oxygen_embed_json_schemas\s*\((?<body>.*?)\)",
                RegexOptions.Singleline,
                TimeSpan.FromSeconds(1)).Matches(
                contents,
                0);

            foreach (Match match in embeddedSchemaBlocks)
            {
                _ = match.Groups["body"].Value.Should().NotContain(
                    ".editor.schema.json",
                    $"editor overlays must not be embedded by {cmakeFile}");
            }
        }
    }

    [TestMethod]
    public void AllEditorOverlays_ShouldUseOnlyEditorAnnotationNamespace()
    {
        var schemaRoot = FindSchemaRoot();
        foreach (var overlayPath in Directory.GetFiles(schemaRoot, "*.editor.schema.json", SearchOption.TopDirectoryOnly))
        {
            var overlay = LoadSchema(schemaRoot, Path.GetFileName(overlayPath));

            var violations = EditorSchemaOverlay.LintAnnotationNamespace(overlay);

            _ = violations.Should().BeEmpty($"{Path.GetFileName(overlayPath)} must not redefine engine schema constraints");
        }
    }

    [TestMethod]
    public void AllEditorOverlays_ShouldReferenceTheirSiblingEngineSchema()
    {
        var schemaRoot = FindSchemaRoot();
        foreach (var overlayPath in Directory.GetFiles(schemaRoot, "*.editor.schema.json", SearchOption.TopDirectoryOnly))
        {
            var overlayFileName = Path.GetFileName(overlayPath);
            var engineFileName = overlayFileName.Replace(".editor.schema.json", ".schema.json", StringComparison.OrdinalIgnoreCase);
            var overlay = LoadSchema(schemaRoot, overlayFileName);

            _ = OverlayReferencesEngineSchema(overlay, engineFileName).Should().BeTrue(
                $"{overlayFileName} must compose with {engineFileName} instead of standing alone");
        }
    }

    [TestMethod]
    public void AllEditorOverlays_ShouldCoverAuthorableEngineFields()
    {
        var schemaRoot = FindSchemaRoot();
        foreach (var overlayPath in Directory.GetFiles(schemaRoot, "*.editor.schema.json", SearchOption.TopDirectoryOnly))
        {
            var overlayFileName = Path.GetFileName(overlayPath);
            var engineFileName = overlayFileName.Replace(".editor.schema.json", ".schema.json", StringComparison.OrdinalIgnoreCase);
            var engine = LoadSchema(schemaRoot, engineFileName);
            var overlay = LoadSchema(schemaRoot, overlayFileName);

            var missing = EditorSchemaOverlay.FindMissingAnnotationCoverage(
                engine,
                overlay,
                GetHiddenAuthoringPaths(engineFileName));

            _ = missing.Should().BeEmpty($"{overlayFileName} must annotate every visible authoring path");
        }
    }

    private static JsonObject LoadSchema(string schemaRoot, string fileName)
        => JsonNode.Parse(File.ReadAllText(Path.Combine(schemaRoot, fileName)))!.AsObject();

    private static bool OverlayReferencesEngineSchema(JsonObject overlay, string engineFileName)
    {
        if (string.Equals(overlay["$ref"]?.GetValue<string>(), engineFileName, StringComparison.Ordinal))
        {
            return true;
        }

        if (overlay["allOf"] is not JsonArray allOf)
        {
            return false;
        }

        return allOf
            .OfType<JsonObject>()
            .Any(entry => string.Equals(entry["$ref"]?.GetValue<string>(), engineFileName, StringComparison.Ordinal));
    }

    private static IReadOnlyList<string> GetHiddenAuthoringPaths(string engineFileName)
        => engineFileName switch
        {
            "oxygen.material-descriptor.schema.json" => ["/$schema"],
            "oxygen.transform-component.schema.json" => ["/kind"],
            _ => [],
        };

    private static string FindSchemaRoot()
        => Path.Combine(
            FindRepoRoot(),
            "projects",
            "Oxygen.Engine",
            "src",
            "Oxygen",
            "Cooker",
            "Import",
            "Schemas");

    private static string FindRepoRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            if (File.Exists(Path.Combine(current.FullName, "design", "editor", "lld", "property-pipeline-redesign.md"))
                && Directory.Exists(Path.Combine(current.FullName, "projects", "Oxygen.Engine")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the DroidNet repository root.");
    }
}
