// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text.Json.Nodes;
using Json.Schema;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Validates authored material JSON against the engine schema embedded
/// into the cooker, and against the merged engine + overlay schema used
/// by the editor.
/// </summary>
/// <remarks>
/// <para>
/// This is the cooking-side proof of the property pipeline: the editor
/// can validate authored material descriptors against the SAME engine
/// schema that the cooker (<c>oxgn-cook</c>, <c>pakgen</c>) embeds at
/// build time via the <c>oxygen_embed_json_schemas(...)</c> CMake macro.
/// The overlay (<c>oxygen.material-descriptor.editor.schema.json</c>)
/// $refs the engine schema and adds <c>x-editor-*</c> annotations only —
/// JSON Schema's "ignore unknown keywords" rule guarantees the merged
/// validator accepts exactly the same set of authored documents.
/// </para>
/// </remarks>
public sealed class MaterialSchemaValidator
{
    /// <summary>
    /// File name of the engine material descriptor schema.
    /// </summary>
    public const string EngineSchemaFileName = "oxygen.material-descriptor.schema.json";

    /// <summary>
    /// File name of the editor overlay for the material descriptor schema.
    /// </summary>
    public const string EditorOverlayFileName = "oxygen.material-descriptor.editor.schema.json";

    private readonly EditorSchemaCatalog catalog;

    /// <summary>
    /// Initializes a new instance from a schema catalog.
    /// </summary>
    /// <param name="catalog">The catalog.</param>
    public MaterialSchemaValidator(EditorSchemaCatalog catalog)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        this.catalog = catalog;
    }

    /// <summary>
    /// Loads the catalog from the directory hosting <c>Oxygen.Editor.Schemas.dll</c>'s
    /// <c>Schemas</c> output folder. The schemas are placed there at build time by
    /// <c>Oxygen.Editor.Schemas.csproj</c>'s content globs.
    /// </summary>
    /// <returns>The loaded validator.</returns>
    public static MaterialSchemaValidator LoadFromAssemblyOutput()
    {
        var directory = LocateSchemasDirectory();
        var catalog = EditorSchemaCatalog.LoadFromDirectory(directory);
        return new MaterialSchemaValidator(catalog);
    }

    /// <summary>
    /// Validates a material document against the engine schema only.
    /// </summary>
    /// <param name="materialJson">The material JSON document.</param>
    /// <returns>The validation result.</returns>
    public MaterialValidationResult ValidateAgainstEngineSchema(JsonNode? materialJson)
    {
        var json = this.catalog.GetEngineSchema(EngineSchemaFileName);
        var schema = JsonSchema.FromText(json.ToJsonString());
        var evaluation = schema.Evaluate(ToElement(materialJson), BuildOptions());
        return ToResult(evaluation, Source: "engine");
    }

    /// <summary>
    /// Validates a material document against the merged engine + overlay
    /// schema. By spec, this MUST accept exactly the same set of
    /// documents as <see cref="ValidateAgainstEngineSchema"/>.
    /// </summary>
    /// <param name="materialJson">The material JSON document.</param>
    /// <returns>The validation result.</returns>
    public MaterialValidationResult ValidateAgainstMergedSchema(JsonNode? materialJson)
    {
        var ok = this.catalog.ValidateAgainstMerged(EngineSchemaFileName, materialJson);
        return new MaterialValidationResult(ok, Source: "merged", []);
    }

    /// <summary>
    /// Lints the editor overlay: returns the offending non-annotation
    /// keywords, if any. The expected result is an empty list.
    /// </summary>
    /// <returns>The list of overlay violations.</returns>
    public IReadOnlyList<(string Pointer, string Keyword)> LintOverlay()
    {
        var overlay = this.catalog.TryGetOverlay(EngineSchemaFileName)
            ?? throw new InvalidOperationException("Material editor overlay schema is not present in the catalog.");
        return EditorSchemaOverlay.LintAnnotationNamespace(overlay);
    }

    /// <summary>
    /// Asserts that authored documents accepted by the engine schema are
    /// also accepted by the merged engine + overlay schema. Used by
    /// editor-side validators when saving a material.
    /// </summary>
    /// <param name="materialJson">The material JSON document.</param>
    /// <returns>True when both validations agree (both accept or both
    /// reject).</returns>
    public bool ValidatorParityHolds(JsonNode? materialJson)
    {
        var engine = this.ValidateAgainstEngineSchema(materialJson).IsValid;
        var merged = this.ValidateAgainstMergedSchema(materialJson).IsValid;
        return engine == merged;
    }

    private static EvaluationOptions BuildOptions() => new()
    {
        OutputFormat = OutputFormat.List,
    };

    private static System.Text.Json.JsonElement ToElement(JsonNode? node)
        => node is null ? default : System.Text.Json.JsonSerializer.SerializeToElement(node);

    private static MaterialValidationResult ToResult(EvaluationResults evaluation, string Source)
    {
        if (evaluation.IsValid)
        {
            return new MaterialValidationResult(true, Source, []);
        }

        var errors = new List<string>();
        FlattenErrors(evaluation, errors);
        return new MaterialValidationResult(false, Source, errors);
    }

    private static void FlattenErrors(EvaluationResults node, List<string> errors)
    {
        if (node.Errors is not null)
        {
            foreach (var (key, value) in node.Errors)
            {
                errors.Add($"{node.InstanceLocation}: {key} {value}");
            }
        }

        if (node.Details is null)
        {
            return;
        }

        foreach (var detail in node.Details)
        {
            FlattenErrors(detail, errors);
        }
    }

    private static string LocateSchemasDirectory()
    {
        // Schemas live next to Oxygen.Editor.Schemas.dll (its csproj
        // copies the engine schema files into a Schemas/ output folder).
        // The MaterialEditor's bin/ directory may or may not have them
        // depending on copy-local; resolve via the Schemas assembly.
        var schemasAssemblyDir = Path.GetDirectoryName(typeof(EditorSchemaCatalog).Assembly.Location)
            ?? AppContext.BaseDirectory;
        var primary = Path.Combine(schemasAssemblyDir, "Schemas");
        if (Directory.Exists(primary))
        {
            return primary;
        }

        var assemblyDir = Path.GetDirectoryName(typeof(MaterialSchemaValidator).Assembly.Location)
            ?? AppContext.BaseDirectory;
        var candidate = Path.Combine(assemblyDir, "Schemas");
        if (Directory.Exists(candidate))
        {
            return candidate;
        }

        // Fallback: search up to three levels up for a Schemas dir.
        var current = new DirectoryInfo(assemblyDir);
        for (var i = 0; i < 3 && current is not null; i++)
        {
            var probe = Path.Combine(current.FullName, "Schemas");
            if (Directory.Exists(probe))
            {
                return probe;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException($"Could not find Schemas/ next to {schemasAssemblyDir} or {assemblyDir}.");
    }
}

/// <summary>
/// The validation outcome reported by <see cref="MaterialSchemaValidator"/>.
/// </summary>
/// <param name="IsValid">Whether the document was accepted.</param>
/// <param name="Source">A short label for which schema produced the result
/// (<c>"engine"</c> or <c>"merged"</c>).</param>
/// <param name="Errors">The list of error strings; empty when the document
/// is valid.</param>
public readonly record struct MaterialValidationResult(bool IsValid, string Source, IReadOnlyList<string> Errors);
