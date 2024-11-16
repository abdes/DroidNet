// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using System.Diagnostics;

internal static class UrlParserHelpers
{
    public static bool PeekStartsWith(this ref ReadOnlySpan<char> remaining, char c)
        => remaining.Length > 0 && remaining[0] == c;

    public static bool PeekStartsWith(this ref ReadOnlySpan<char> remaining, string prefix)
        => remaining.Length >= prefix.Length && remaining[..prefix.Length].SequenceEqual(prefix.AsSpan());

    public static bool PeekContains(this ref ReadOnlySpan<char> remaining, int index, string prefix)
        => remaining.Length >= index + prefix.Length &&
           remaining[index..(index + prefix.Length)].SequenceEqual(prefix.AsSpan());

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

    public static bool ConsumeOptional(this ref ReadOnlySpan<char> remaining, char c)
    {
        if (remaining.Length == 0 || remaining[0] != c)
        {
            return false;
        }

        remaining = remaining[1..];
        return true;
    }

    public static void Capture(this ref ReadOnlySpan<char> remaining, char c)
    {
        var consumed = remaining.ConsumeOptional(c);
        Debug.Assert(consumed, $"{nameof(Capture)} was expecting to find '{c}' but it was not there.");
    }

    public static void Capture(this ref ReadOnlySpan<char> remaining, string str)
    {
        if (str.Length == 0)
        {
            return;
        }

        var consumed = remaining.ConsumeOptional(str);
        Debug.Assert(consumed, $"{nameof(Capture)} was expecting to find '{str}' but it was not there.");
    }

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

    public static string MatchMatrixParamValue(this ref ReadOnlySpan<char> remaining) => remaining.MatchSegment();

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
