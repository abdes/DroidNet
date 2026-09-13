// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;

namespace Oxygen.Editor.ContentBrowser.Shell;

/// <summary>
///     Centralizes translation between route query parameters (strings) and <see cref="ContentBrowserState" />.
/// </summary>
/// <remarks>
///     This is intentionally kept in an existing namespace (Shell) to avoid creating additional namespace structure.
/// </remarks>
internal static class RouteStateMapping
{
    /// <summary>The query key identifying selected browser folders.</summary>
    internal const string SelectedQueryKey = "selected";

    /// <summary>Reads the complete scope from a successful local navigation URL.</summary>
    /// <param name="url">The local navigation URL.</param>
    /// <returns>All selected folders, in URL order.</returns>
    internal static IReadOnlyList<string> ParseSelectedFoldersFromUrl(string? url)
    {
        if (string.IsNullOrEmpty(url))
        {
            return [];
        }

        var qIndex = url.IndexOf('?', StringComparison.Ordinal);
        if (qIndex < 0 || qIndex >= url.Length - 1)
        {
            return [];
        }

        var query = url[(qIndex + 1)..];
        var folders = new List<string>();
        foreach (var pair in query.Split('&', StringSplitOptions.RemoveEmptyEntries))
        {
            var kv = pair.Split('=', 2);
            if (kv.Length == 2 && string.Equals(kv[0], SelectedQueryKey, StringComparison.Ordinal))
            {
                var folder = Uri.UnescapeDataString(kv[1]);
                if (!string.IsNullOrWhiteSpace(folder))
                {
                    folders.Add(folder);
                }
            }
        }

        return folders;
    }

    /// <summary>Applies a completed navigation as one observable scope update.</summary>
    /// <param name="state">The shared browser scope.</param>
    /// <param name="url">The completed navigation URL.</param>
    internal static void ApplyNavigationScope(ContentBrowserState state, string? url)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!string.IsNullOrEmpty(url))
        {
            state.SetSelectedFolders(ParseSelectedFoldersFromUrl(url));
        }
    }

    /// <summary>Reads the folder scope available during route activation.</summary>
    /// <param name="route">The activated route.</param>
    /// <returns>The selected nonempty folder paths.</returns>
    internal static IReadOnlyList<string> GetSelectedFolders(IActiveRoute? route)
    {
        var values = route?.QueryParams?.GetValues(SelectedQueryKey);
        if (values is null)
        {
            return [];
        }

        // Match existing behavior: ignore empty entries.
        return values
            .Where(v => !string.IsNullOrEmpty(v))
            .Select(v => v!)
            .ToArray();
    }

    /// <summary>Encodes a deterministic folder scope for local navigation and history.</summary>
    /// <param name="selectedFolders">The selected browser folders.</param>
    /// <returns>The URL query, or an empty string for the project-wide scope.</returns>
    internal static string BuildSelectedQuery(IEnumerable<string> selectedFolders)
    {
        ArgumentNullException.ThrowIfNull(selectedFolders);

        // Keep URL generation deterministic.
        var sortedFolders = selectedFolders.Order(StringComparer.Ordinal);
        var selectedParams = string.Join('&', sortedFolders.Select(folder => $"{SelectedQueryKey}={Uri.EscapeDataString(folder)}"));

        return string.IsNullOrEmpty(selectedParams) ? string.Empty : $"?{selectedParams}";
    }
}
