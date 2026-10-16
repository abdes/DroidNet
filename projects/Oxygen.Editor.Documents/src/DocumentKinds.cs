// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>Matches file names against the suffixes of document kinds.</summary>
internal static class DocumentKinds
{
    /// <summary>Gets the longest suffix of a kind that ends a file's name.</summary>
    /// <param name="kind">The kind.</param>
    /// <param name="path">The file's path.</param>
    /// <returns>The suffix, or <see langword="null"/> when the kind does not open the file.</returns>
    public static string? SuffixOf(IDocumentKind kind, string path)
    {
        var name = Path.GetFileName(path);
        return kind.FileSuffixes
            .Where(suffix => name.Length > suffix.Length && name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            .MaxBy(static suffix => suffix.Length);
    }

    /// <summary>Gets a file's name without the suffix of its kind.</summary>
    /// <param name="kind">The kind.</param>
    /// <param name="path">The file's path.</param>
    /// <returns>The document's title.</returns>
    public static string TitleOf(IDocumentKind kind, string path)
    {
        var name = Path.GetFileName(path);
        return SuffixOf(kind, path) is { } suffix ? name[..^suffix.Length] : name;
    }
}
