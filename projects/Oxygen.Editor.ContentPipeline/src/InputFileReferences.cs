// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Finds the actions a hand-written input file uses but does not declare.
/// </summary>
/// <remarks>
/// Until the engine resolves references between input files by virtual path, only self-contained files cook: a
/// mapping, chord or combo step that names another file's action would make the result depend on job order.
/// </remarks>
public static class InputFileReferences
{
    /// <summary>The suffix of a mapping-context input file.</summary>
    public const string InputFileSuffix = ".input.json";

    /// <summary>The suffix of a single-action input file.</summary>
    public const string InputActionFileSuffix = ".input-action.json";

    /// <summary>Checks whether a path names a hand-written input file.</summary>
    /// <param name="path">The path or virtual path.</param>
    /// <returns><see langword="true"/> for <c>*.input.json</c> and <c>*.input-action.json</c>.</returns>
    public static bool IsInputFile(string path)
        => path.EndsWith(InputFileSuffix, StringComparison.OrdinalIgnoreCase)
           || path.EndsWith(InputActionFileSuffix, StringComparison.OrdinalIgnoreCase);

    /// <summary>Finds the action names an input file uses without declaring them.</summary>
    /// <param name="json">The input file text.</param>
    /// <returns>The undeclared names in first-use order; empty when the file is self-contained or not valid JSON,
    /// which the cooker reports itself.</returns>
    public static IReadOnlyList<string> FindUndeclaredActions(string json)
    {
        ArgumentNullException.ThrowIfNull(json);
        try
        {
            using var document = JsonDocument.Parse(json, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("contexts", out var contexts) || contexts.ValueKind != JsonValueKind.Array)
            {
                return [];
            }

            var declared = new HashSet<string>(StringComparer.Ordinal);
            if (root.TryGetProperty("actions", out var actions) && actions.ValueKind == JsonValueKind.Array)
            {
                declared.UnionWith(actions.EnumerateArray().Select(static action => Name(action, "name")).OfType<string>());
            }

            var used = new List<string>();
            foreach (var mapping in Items(contexts).SelectMany(static context => Items(context, "mappings")))
            {
                used.AddIfNotNull(Name(mapping, "action"));
                var triggers = mapping.TryGetProperty("trigger", out var single) && single.ValueKind == JsonValueKind.Object
                    ? [single]
                    : Items(mapping, "triggers").ToArray();
                foreach (var trigger in triggers)
                {
                    used.AddIfNotNull(Name(trigger, "chord_action"));
                    used.AddRange(Items(trigger, "combo_actions").Concat(Items(trigger, "aux")).Select(static step => Name(step, "action")).OfType<string>());
                }
            }

            return [.. used.Where(name => !declared.Contains(name)).Distinct(StringComparer.Ordinal)];
        }
        catch (JsonException)
        {
            return [];
        }
    }

    /// <summary>Finds the priority an input file gives a mapping context it declares.</summary>
    /// <param name="json">The input file text.</param>
    /// <param name="contextName">The context name.</param>
    /// <returns>The declared priority, <c>0</c> when the context declares none, or <see langword="null"/> when the
    /// file does not declare the context or is not valid JSON.</returns>
    public static int? FindContextPriority(string json, string contextName)
    {
        ArgumentNullException.ThrowIfNull(json);
        ArgumentNullException.ThrowIfNull(contextName);
        try
        {
            using var document = JsonDocument.Parse(json, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("contexts", out var contexts))
            {
                return null;
            }

            foreach (var context in Items(contexts).Where(context => string.Equals(Name(context, "name"), contextName, StringComparison.Ordinal)))
            {
                return context.TryGetProperty("priority", out var priority) && priority.ValueKind == JsonValueKind.Number && priority.TryGetInt32(out var value)
                    ? value
                    : 0;
            }

            return null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static string? Name(JsonElement element, string property)
        => element.ValueKind == JsonValueKind.Object && element.TryGetProperty(property, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static IEnumerable<JsonElement> Items(JsonElement array)
        => array.ValueKind == JsonValueKind.Array ? array.EnumerateArray() : [];

    private static IEnumerable<JsonElement> Items(JsonElement element, string property)
        => element.ValueKind == JsonValueKind.Object && element.TryGetProperty(property, out var value) ? Items(value) : [];

    private static void AddIfNotNull(this List<string> names, string? name)
    {
        if (name is not null)
        {
            names.Add(name);
        }
    }
}
