// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace DroidNet.Routing.Detail;

/// <summary>
/// Provides helper methods for parsing URLs within the routing system.
/// </summary>
/// <remarks>
/// The <see cref="UrlParserHelpers"/> class contains utility methods that assist in parsing and
/// processing URLs. These methods are used to break down URLs into their constituent parts, such as
/// segments and query parameters, and to handle special cases like encoded characters and reserved
/// symbols.
/// </remarks>
internal static class UrlParserHelpers
{
    /// <summary>
    /// Checks if the remaining span starts with the specified character.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to check.</param>
    /// <param name="c">The character to check for at the start of the span.</param>
    /// <returns><see langword="true"/> if the remaining span starts with the specified character; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method is useful for quickly determining if the next character in the span matches the
    /// specified character. It is often used in parsing logic to handle specific delimiters or
    /// markers in the URL.
    /// </remarks>
    public static bool PeekStartsWith(this ref ReadOnlySpan<char> remaining, char c)
        => remaining.Length > 0 && remaining[0] == c;

    /// <summary>
    /// Checks if the remaining span starts with the specified prefix.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to check.</param>
    /// <param name="prefix">The prefix to check for at the start of the span.</param>
    /// <returns><see langword="true"/> if the remaining span starts with the specified prefix; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method is useful for quickly determining if the next sequence of characters in the span
    /// matches the specified prefix. It is often used in parsing logic to handle specific patterns
    /// or keywords in the URL.
    /// </remarks>
    public static bool PeekStartsWith(this ref ReadOnlySpan<char> remaining, string prefix)
        => remaining.Length >= prefix.Length && remaining[..prefix.Length].SequenceEqual(prefix.AsSpan());

    /// <summary>
    /// Checks if the remaining span contains the specified prefix at the given index.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to check.</param>
    /// <param name="index">The index at which to check for the prefix.</param>
    /// <param name="prefix">The prefix to check for at the specified index.</param>
    /// <returns><see langword="true"/> if the remaining span contains the specified prefix at the given index; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method is useful for determining if a specific pattern or keyword appears at a
    /// particular position within the span. It is often used in parsing logic to handle nested
    /// structures or specific segments in the URL.
    /// </remarks>
    public static bool PeekContains(this ref ReadOnlySpan<char> remaining, int index, string prefix)
        => remaining.Length >= index + prefix.Length &&
           remaining[index..(index + prefix.Length)].SequenceEqual(prefix.AsSpan());

    /// <summary>
    /// Consumes the specified string from the start of the remaining span if it is present.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to consume from.</param>
    /// <param name="str">The string to consume if present.</param>
    /// <returns><see langword="true"/> if the string was consumed; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method is useful for conditionally consuming specific patterns or delimiters from the
    /// start of the span. It is often used in parsing logic to handle optional components in the
    /// URL.
    /// </remarks>
    public static bool ConsumeOptional(this ref ReadOnlySpan<char> remaining, string str)
    {
        var initialLength = remaining.Length;

        var start = 0;
        for (; start < str.Length && start < remaining.Length; start++)
        {
            var found = false;
            foreach (var consumeChar in str)
            {
                if (remaining[start] == consumeChar)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                break;
            }
        }

        remaining = remaining[start..];

        return initialLength != remaining.Length;
    }

    /// <summary>
    /// Consumes the specified character from the start of the remaining span if it is present.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to consume from.</param>
    /// <param name="c">The character to consume if present.</param>
    /// <returns><see langword="true"/> if the character was consumed; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// This method is useful for conditionally consuming specific characters from the start of the
    /// span. It is often used in parsing logic to handle optional delimiters or markers in the URL.
    /// </remarks>
    public static bool ConsumeOptional(this ref ReadOnlySpan<char> remaining, char c)
    {
        if (remaining.Length == 0 || remaining[0] != c)
        {
            return false;
        }

        remaining = remaining[1..];
        return true;
    }

    /// <summary>
    /// Ensures that the specified character is present at the start of the remaining span and consumes it.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to consume from.</param>
    /// <param name="c">The character to consume.</param>
    /// <remarks>
    /// This method is useful for asserting that a specific character is present at the start of the
    /// span and consuming it. It is often used in parsing logic to handle mandatory delimiters or
    /// markers in the URL.
    /// </remarks>
    public static void Capture(this ref ReadOnlySpan<char> remaining, char c)
    {
        var consumed = remaining.ConsumeOptional(c);
        Debug.Assert(consumed, $"{nameof(Capture)} was expecting to find '{c}' but it was not there.");
    }

    /// <summary>
    /// Ensures that the specified string is present at the start of the remaining span and consumes it.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to consume from.</param>
    /// <param name="str">The string to consume.</param>
    /// <remarks>
    /// This method is useful for asserting that a specific string is present at the start of the
    /// span and consuming it. It is often used in parsing logic to handle mandatory patterns or
    /// keywords in the URL.
    /// </remarks>
    public static void Capture(this ref ReadOnlySpan<char> remaining, string str)
    {
        if (str.Length == 0)
        {
            return;
        }

        var consumed = remaining.ConsumeOptional(str);
        Debug.Assert(consumed, $"{nameof(Capture)} was expecting to find '{str}' but it was not there.");
    }

    /// <summary>
    /// Matches a segment in the remaining path of a URL.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to match against.</param>
    /// <returns>The matched segment as a string.</returns>
    /// <remarks>
    /// This method is useful for extracting a segment from the remaining path of a URL. It handles
    /// various delimiters and reserved characters to ensure that the segment is correctly
    /// identified.
    /// </remarks>
    public static string MatchSegment(this ref ReadOnlySpan<char> remaining)
    {
        var index = 0;
        while (index < remaining.Length
               && remaining[index] != '/' && !remaining.PeekContains(index, "//") && remaining[index] != '('
               && remaining[index] != ')' && remaining[index] != '?' && remaining[index] != ';'
               && remaining[index] != '#')
        {
            index++;
        }

        return remaining[..index].ToString();
    }

    /// <summary>
    /// Matches a matrix parameter key in the remaining path of a URL.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to match against.</param>
    /// <returns>The matched matrix parameter key as a string.</returns>
    /// <remarks>
    /// This method is useful for extracting a matrix parameter key from the remaining path of a URL.
    /// It handles various delimiters and reserved characters to ensure that the key is correctly identified.
    /// </remarks>
    public static string MatchMatrixParamKey(this ref ReadOnlySpan<char> remaining)
    {
        var index = 0;
        while (index < remaining.Length
               && remaining[index] != '/' && !remaining.PeekContains(index, "//") && remaining[index] != '('
               && remaining[index] != ')' && remaining[index] != '?' && remaining[index] != ';'
               && remaining[index] != '=' && remaining[index] != '#')
        {
            index++;
        }

        return remaining[..index].ToString();
    }

    /// <summary>
    /// Matches a matrix parameter value in the remaining path of a URL.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to match against.</param>
    /// <returns>The matched matrix parameter value as a string.</returns>
    /// <remarks>
    /// This method is useful for extracting a matrix parameter value from the remaining path of a URL.
    /// It handles various delimiters and reserved characters to ensure that the value is correctly identified.
    /// </remarks>
    public static string MatchMatrixParamValue(this ref ReadOnlySpan<char> remaining) => remaining.MatchSegment();

    /// <summary>
    /// Matches a query parameter key in the remaining path of a URL.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to match against.</param>
    /// <returns>The matched query parameter key as a string.</returns>
    /// <remarks>
    /// This method is useful for extracting a query parameter key from the remaining path of a URL.
    /// It handles various delimiters and reserved characters to ensure that the key is correctly identified.
    /// </remarks>
    public static string MatchQueryParamKey(this ref ReadOnlySpan<char> remaining)
    {
        var index = 0;
        while (index < remaining.Length
               && !remaining.PeekContains(index, "//") && remaining[index] != '=' && remaining[index] != '?' &&
               remaining[index] != '&' && remaining[index] != '#')
        {
            index++;
        }

        return remaining[..index].ToString();
    }

    /// <summary>
    /// Matches a query parameter value in the remaining path of a URL.
    /// </summary>
    /// <param name="remaining">The remaining span of characters to match against.</param>
    /// <returns>The matched query parameter value as a string.</returns>
    /// <remarks>
    /// This method is useful for extracting a query parameter value from the remaining path of a URL.
    /// It handles various delimiters and reserved characters to ensure that the value is correctly identified.
    /// </remarks>
    public static string MatchQueryParamValue(this ref ReadOnlySpan<char> remaining)
    {
        var index = 0;
        while (index < remaining.Length && !remaining.PeekContains(index, "//") && remaining[index] != '&' &&
               remaining[index] != '#')
        {
            index++;
        }

        return remaining[..index].ToString();
    }
}
