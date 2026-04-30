// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using AwesomeAssertions;

namespace Oxygen.Editor.Schemas.Tests;

/// <summary>
/// Tests editor schema overlay loading, linting, and coverage behavior.
/// </summary>
[TestClass]
public sealed class EditorSchemaOverlayTests
{
    /// <summary>
    /// Verifies that overlay files cannot add validation keywords outside the editor annotation namespace.
    /// </summary>
    [TestMethod]
    public void LintAnnotationNamespaceReportsValidationKeywordViolations()
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

    /// <summary>
    /// Verifies that x-editor annotations are extracted into JSON-pointer keyed metadata.
    /// </summary>
    [TestMethod]
    public void ExtractAnnotationsIndexesAnnotationsByJsonPointer()
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

    /// <summary>
    /// Verifies that authorable engine-schema leaves missing overlay widgets are reported.
    /// </summary>
    [TestMethod]
    public void FindMissingAnnotationCoverageReportsOmittedLeafWidgets()
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

    /// <summary>
    /// Verifies transform overlay coverage against the engine transform schema.
    /// </summary>
    [TestMethod]
    public void TransformOverlayCoversAllAuthorableEngineFields()
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

    /// <summary>
    /// Verifies material overlay coverage against the engine material schema.
    /// </summary>
    [TestMethod]
    public void MaterialOverlayCoversAllAuthorableEngineFields()
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

    /// <summary>
    /// Verifies editor overlay files are not embedded into cooker or PakTool schema resources.
    /// </summary>
    [TestMethod]
    public void CookerEmbeddedSchemasDoNotIncludeEditorOverlays()
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

    /// <summary>
    /// Verifies every editor overlay uses only x-editor annotation keywords plus schema structure.
    /// </summary>
    [TestMethod]
    public void AllEditorOverlaysUseOnlyEditorAnnotationNamespace()
    {
        var schemaRoot = FindSchemaRoot();
        foreach (var overlayPath in Directory.GetFiles(schemaRoot, "*.editor.schema.json", SearchOption.TopDirectoryOnly))
        {
            var overlay = LoadSchema(schemaRoot, Path.GetFileName(overlayPath));

            var violations = EditorSchemaOverlay.LintAnnotationNamespace(overlay);

            _ = violations.Should().BeEmpty($"{Path.GetFileName(overlayPath)} must not redefine engine schema constraints");
        }
    }

    /// <summary>
    /// Verifies every editor overlay composes with its sibling engine schema.
    /// </summary>
    [TestMethod]
    public void AllEditorOverlaysReferenceTheirSiblingEngineSchema()
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

    /// <summary>
    /// Verifies every visible authoring path in every engine schema has editor overlay metadata.
    /// </summary>
    [TestMethod]
    public void AllEditorOverlaysCoverAuthorableEngineFields()
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
