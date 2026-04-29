// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
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
}
