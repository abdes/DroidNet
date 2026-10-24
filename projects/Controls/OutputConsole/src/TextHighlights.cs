// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Documents;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     Builds the highlighter that marks search matches in a text element.
/// </summary>
internal static class TextHighlights
{
    /// <summary>
    ///     Creates a highlighter over every occurrence of the search text, ignoring case.
    /// </summary>
    /// <param name="content">The text shown.</param>
    /// <param name="search">The trimmed search text.</param>
    /// <param name="isCurrent">Whether these are the current find match's occurrences.</param>
    /// <param name="brushes">The theme brushes.</param>
    /// <returns>The highlighter, or <see langword="null" /> when nothing matches.</returns>
    public static TextHighlighter? Create(string content, string search, bool isCurrent, ConsoleBrushes brushes)
    {
        if (search.Length == 0)
        {
            return null;
        }

        TextHighlighter? highlighter = null;
        var index = content.IndexOf(search, StringComparison.OrdinalIgnoreCase);
        while (index >= 0)
        {
            highlighter ??= isCurrent
                ? new TextHighlighter { Background = brushes.CurrentMatchBackground, Foreground = brushes.CurrentMatchForeground }
                : new TextHighlighter { Background = brushes.MatchBackground, Foreground = brushes.MatchForeground };
            highlighter.Ranges.Add(new TextRange { StartIndex = index, Length = search.Length });
            index = content.IndexOf(search, index + search.Length, StringComparison.OrdinalIgnoreCase);
        }

        return highlighter;
    }
}
