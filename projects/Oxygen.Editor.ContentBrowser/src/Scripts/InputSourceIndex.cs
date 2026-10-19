// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.RegularExpressions;
using Oxygen.Managed.Assets;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Finds the hand-written input file that declares an input action or mapping context.
/// </summary>
/// <remarks>
/// Input files cook their actions and contexts into their own folder, so the declaring file is one of the input files
/// in the cooked asset's folder.
/// </remarks>
public static partial class InputSourceIndex
{
    /// <summary>Finds the declaration of an action or mapping context.</summary>
    /// <param name="folder">The folder of the cooked asset and of its source.</param>
    /// <param name="name">The action or context name.</param>
    /// <param name="isContext">Whether to look for a mapping context rather than an action.</param>
    /// <returns>The declaring file and the 1-based line of the declaration, or <see langword="null"/>.</returns>
    public static InputDeclaration? Find(string folder, string name, bool isContext)
    {
        if (!Directory.Exists(folder))
        {
            return null;
        }

        foreach (var file in Directory.EnumerateFiles(folder).Where(static file => AssetFileTypes.Is(file, AssetFileCategory.Input)).Order(StringComparer.OrdinalIgnoreCase))
        {
            string text;
            try
            {
                text = File.ReadAllText(file);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                continue;
            }

            if (Declares(text, file, name, isContext))
            {
                return new InputDeclaration(file, FindNameLine(text, name));
            }
        }

        return null;
    }

    private static bool Declares(string text, string file, string name, bool isContext)
    {
        try
        {
            using var document = JsonDocument.Parse(text, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
            {
                return false;
            }

            if (file.EndsWith(AssetFileTypes.InputActionFile, StringComparison.Ordinal))
            {
                return !isContext && HasName(root, name);
            }

            return root.TryGetProperty(isContext ? "contexts" : "actions", out var list)
                && list.ValueKind == JsonValueKind.Array
                && list.EnumerateArray().Any(item => item.ValueKind == JsonValueKind.Object && HasName(item, name));
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static bool HasName(JsonElement element, string name)
        => element.TryGetProperty("name", out var value) && value.ValueKind == JsonValueKind.String
            && string.Equals(value.GetString(), name, StringComparison.Ordinal);

    private static int FindNameLine(string text, string name)
    {
        foreach (Match match in NamePropertyPattern().Matches(text))
        {
            if (string.Equals(match.Groups["value"].Value, name, StringComparison.Ordinal))
            {
                return 1 + text.AsSpan(0, match.Index).Count('\n');
            }
        }

        return 1;
    }

    [GeneratedRegex("\"name\"\\s*:\\s*\"(?<value>[^\"]*)\"", RegexOptions.CultureInvariant, matchTimeoutMilliseconds: 1000)]
    private static partial Regex NamePropertyPattern();
}
