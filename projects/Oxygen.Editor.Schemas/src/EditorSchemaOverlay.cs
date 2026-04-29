// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;

namespace Oxygen.Editor.Schemas;

/// <summary>
/// Merges an engine schema with its editor overlay by JSON Pointer and
/// extracts <see cref="EditorAnnotation"/> objects per leaf path.
/// </summary>
public static class EditorSchemaOverlay
{
    /// <summary>
    /// Standard JSON Schema composition keywords that the overlay is
    /// allowed to use for structure. Anything outside this set, that does
    /// not start with the <see cref="AnnotationPrefix"/>, is a lint error.
    /// </summary>
    public static readonly IReadOnlySet<string> AllowedStructuralKeywords = new HashSet<string>(StringComparer.Ordinal)
    {
        "$schema",
        "$id",
        "$ref",
        "$comment",
        "title",
        "description",
        "definitions",
        "allOf",
        "properties",
        "items",
    };

    /// <summary>
    /// The reserved annotation keyword namespace prefix.
    /// </summary>
    public const string AnnotationPrefix = "x-editor-";

    /// <summary>
    /// Extracts editor annotations indexed by JSON Pointer.
    /// </summary>
    /// <param name="overlay">The overlay schema document.</param>
    /// <returns>The map from pointer to parsed annotation.</returns>
    public static IReadOnlyDictionary<string, EditorAnnotation> ExtractAnnotations(JsonObject overlay)
    {
        ArgumentNullException.ThrowIfNull(overlay);

        var result = new Dictionary<string, EditorAnnotation>(StringComparer.Ordinal);
        Walk(overlay, string.Empty, result);
        return result;
    }

    /// <summary>
    /// Performs the annotation-namespace lint: returns the list of
    /// non-annotation, non-structural keywords the overlay illegally
    /// declares.
    /// </summary>
    /// <param name="overlay">The overlay schema document.</param>
    /// <returns>The list of violations as
    /// <c>(jsonPointer, offendingKeyword)</c> tuples.</returns>
    public static IReadOnlyList<(string Pointer, string Keyword)> LintAnnotationNamespace(JsonObject overlay)
    {
        ArgumentNullException.ThrowIfNull(overlay);
        var violations = new List<(string, string)>();
        LintWalk(overlay, string.Empty, violations);
        return violations;
    }

    /// <summary>
    /// Finds engine-schema authoring paths that are not covered by an editor
    /// overlay annotation.
    /// </summary>
    /// <param name="engineSchema">The engine schema document.</param>
    /// <param name="overlay">The editor overlay document.</param>
    /// <param name="hiddenPaths">Paths intentionally hidden from the editor.</param>
    /// <returns>Paths that have neither a complete annotation nor a hidden-path entry.</returns>
    /// <remarks>
    /// The coverage unit is the editor widget boundary. Object nodes rendered
    /// as <c>section</c> recurse into their child properties; object nodes
    /// rendered by any other widget, such as an asset picker, are considered
    /// covered at the parent path. Local <c>#/definitions/...</c> references are
    /// followed so overlays can cover fields declared through reusable engine
    /// schema definitions.
    /// </remarks>
    public static IReadOnlyList<string> FindMissingAnnotationCoverage(
        JsonObject engineSchema,
        JsonObject overlay,
        IEnumerable<string>? hiddenPaths = null)
    {
        ArgumentNullException.ThrowIfNull(engineSchema);
        ArgumentNullException.ThrowIfNull(overlay);

        var annotations = ExtractAnnotations(overlay);
        var hidden = new HashSet<string>(hiddenPaths ?? Array.Empty<string>(), StringComparer.Ordinal);
        var coveragePaths = new List<string>();
        CollectCoveragePaths(engineSchema, engineSchema, string.Empty, annotations, coveragePaths, []);

        return coveragePaths
            .Where(path => !hidden.Contains(path))
            .Where(path => !HasCompleteAnnotation(annotations, path))
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)
            .ToList();
    }

    private static void Walk(JsonObject node, string pointer, Dictionary<string, EditorAnnotation> result)
    {
        var annotation = ParseAnnotation(node);
        if (annotation is not null)
        {
            result[pointer] = annotation;
        }

        // Recurse into "properties" — a JSON Schema "properties" map is
        // not itself a property; its keys are.
        if (node["properties"] is JsonObject props)
        {
            foreach (var (key, child) in props)
            {
                if (child is JsonObject childObj)
                {
                    Walk(childObj, $"{pointer}/{EncodePointerSegment(key)}", result);
                }
            }
        }

        // Recurse into "items" arrays / objects (not used heavily by
        // the editor today, but kept for parity).
        if (node["items"] is JsonObject items)
        {
            Walk(items, $"{pointer}/*", result);
        }

        // Recurse into "definitions" — needed to merge against the engine
        // schema's reusable types.
        if (node["definitions"] is JsonObject defs)
        {
            foreach (var (key, child) in defs)
            {
                if (child is JsonObject childObj)
                {
                    Walk(childObj, $"#/definitions/{EncodePointerSegment(key)}", result);
                }
            }
        }
    }

    private static EditorAnnotation? ParseAnnotation(JsonObject node)
    {
        EditorAnnotation? annotation = null;
        Dictionary<string, object?>? extra = null;

        string? label = null;
        string? group = null;
        int? order = null;
        string? renderer = null;
        string? tooltip = null;
        double? step = null;
        double? softMax = null;
        double? softMin = null;
        var advanced = false;

        foreach (var (key, value) in node)
        {
            if (!key.StartsWith(AnnotationPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            annotation ??= new EditorAnnotation();
            switch (key)
            {
                case "x-editor-label":
                    label = value?.GetValue<string>();
                    break;
                case "x-editor-group":
                    group = value?.GetValue<string>();
                    break;
                case "x-editor-order":
                    order = value?.GetValue<int>();
                    break;
                case "x-editor-renderer":
                    renderer = value?.GetValue<string>();
                    break;
                case "x-editor-tooltip":
                    tooltip = value?.GetValue<string>();
                    break;
                case "x-editor-step":
                    step = value?.GetValue<double>();
                    break;
                case "x-editor-soft-max":
                    softMax = value?.GetValue<double>();
                    break;
                case "x-editor-soft-min":
                    softMin = value?.GetValue<double>();
                    break;
                case "x-editor-advanced":
                    advanced = value?.GetValue<bool>() ?? false;
                    break;
                default:
                    extra ??= new Dictionary<string, object?>(StringComparer.Ordinal);
                    extra[key] = MaterializeValue(value);
                    break;
            }
        }

        if (annotation is null)
        {
            return null;
        }

        return new EditorAnnotation
        {
            Label = label,
            Group = group,
            Order = order,
            Renderer = renderer,
            Tooltip = tooltip,
            Step = step,
            SoftMax = softMax,
            SoftMin = softMin,
            Advanced = advanced,
            Extra = extra ?? new Dictionary<string, object?>(StringComparer.Ordinal),
        };
    }

    private static object? MaterializeValue(JsonNode? value)
    {
        if (value is null)
        {
            return null;
        }

        if (value is JsonValue jv)
        {
            if (jv.TryGetValue<string>(out var s))
            {
                return s;
            }

            if (jv.TryGetValue<bool>(out var b))
            {
                return b;
            }

            if (jv.TryGetValue<double>(out var d))
            {
                return d;
            }
        }

        return value.ToJsonString();
    }

    private static string EncodePointerSegment(string segment)
    {
        // RFC 6901: ~ -> ~0, / -> ~1
        return segment.Replace("~", "~0", StringComparison.Ordinal).Replace("/", "~1", StringComparison.Ordinal);
    }

    private static bool HasCompleteAnnotation(
        IReadOnlyDictionary<string, EditorAnnotation> annotations,
        string pointer)
        => annotations.TryGetValue(pointer, out var annotation)
           && !string.IsNullOrWhiteSpace(annotation.Label)
           && !string.IsNullOrWhiteSpace(annotation.Renderer);

    private static void CollectCoveragePaths(
        JsonObject root,
        JsonObject node,
        string pointer,
        IReadOnlyDictionary<string, EditorAnnotation> annotations,
        List<string> result,
        HashSet<string> refStack)
    {
        var resolved = ResolveLocalReference(root, node, refStack);
        if (resolved["properties"] is not JsonObject properties)
        {
            if (pointer.Length > 0)
            {
                result.Add(pointer);
            }

            return;
        }

        foreach (var (propertyName, child) in properties)
        {
            if (child is not JsonObject childObject)
            {
                continue;
            }

            var childPointer = $"{pointer}/{EncodePointerSegment(propertyName)}";
            var childResolved = ResolveLocalReference(root, childObject, refStack);
            var childHasObjectProperties = childResolved["properties"] is JsonObject;

            if (!childHasObjectProperties || IsCoveredWidgetBoundary(annotations, childPointer))
            {
                result.Add(childPointer);
                continue;
            }

            CollectCoveragePaths(root, childResolved, childPointer, annotations, result, refStack);
        }
    }

    private static bool IsCoveredWidgetBoundary(
        IReadOnlyDictionary<string, EditorAnnotation> annotations,
        string pointer)
        => annotations.TryGetValue(pointer, out var annotation)
           && !string.IsNullOrWhiteSpace(annotation.Renderer)
           && !string.Equals(annotation.Renderer, "section", StringComparison.OrdinalIgnoreCase);

    private static JsonObject ResolveLocalReference(JsonObject root, JsonObject node, HashSet<string> refStack)
    {
        if (node["$ref"]?.GetValue<string>() is not { } reference
            || !reference.StartsWith("#/definitions/", StringComparison.Ordinal))
        {
            return node;
        }

        if (!refStack.Add(reference))
        {
            return node;
        }

        try
        {
            var definitionName = reference["#/definitions/".Length..].Replace("~1", "/", StringComparison.Ordinal).Replace("~0", "~", StringComparison.Ordinal);
            if (root["definitions"] is JsonObject definitions
                && definitions[definitionName] is JsonObject definition)
            {
                return ResolveLocalReference(root, definition, refStack);
            }

            return node;
        }
        finally
        {
            _ = refStack.Remove(reference);
        }
    }

    private static void LintWalk(JsonObject node, string pointer, List<(string, string)> violations)
    {
        foreach (var (key, value) in node)
        {
            if (key.StartsWith(AnnotationPrefix, StringComparison.Ordinal))
            {
                continue;
            }

            if (!AllowedStructuralKeywords.Contains(key))
            {
                violations.Add((pointer, key));
            }
        }

        if (node["properties"] is JsonObject props)
        {
            foreach (var (key, child) in props)
            {
                if (child is JsonObject childObj)
                {
                    LintWalk(childObj, $"{pointer}/{EncodePointerSegment(key)}", violations);
                }
            }
        }

        if (node["items"] is JsonObject items)
        {
            LintWalk(items, $"{pointer}/*", violations);
        }

        if (node["definitions"] is JsonObject defs)
        {
            foreach (var (key, child) in defs)
            {
                if (child is JsonObject childObj)
                {
                    LintWalk(childObj, $"#/definitions/{EncodePointerSegment(key)}", violations);
                }
            }
        }

        if (node["allOf"] is JsonArray allOf)
        {
            for (var i = 0; i < allOf.Count; i++)
            {
                if (allOf[i] is JsonObject allOfChild)
                {
                    LintWalk(allOfChild, $"{pointer}/allOf/{i}", violations);
                }
            }
        }
    }
}
