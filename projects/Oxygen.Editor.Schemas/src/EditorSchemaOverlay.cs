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
