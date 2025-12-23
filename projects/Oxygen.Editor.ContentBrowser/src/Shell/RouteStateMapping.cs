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
    internal const string SelectedQueryKey = "selected";

    internal static string? ParseFirstSelectedFromUrl(string? url)
    {
        if (string.IsNullOrEmpty(url))
        {
            return null;
        }

        var qIndex = url.IndexOf('?', StringComparison.Ordinal);
        if (qIndex < 0 || qIndex >= url.Length - 1)
        {
            return null;
        }

        var query = url[(qIndex + 1)..];
        foreach (var pair in query.Split('&', StringSplitOptions.RemoveEmptyEntries))
        {
            var kv = pair.Split('=', 2);
            if (kv.Length == 2 && string.Equals(kv[0], SelectedQueryKey, StringComparison.Ordinal))
            {
                return Uri.UnescapeDataString(kv[1]);
            }
        }

        return null;
    }

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

    internal static void ApplySelectedFoldersToState(IActiveRoute? route, ContentBrowserState contentBrowserState)
    {
        ArgumentNullException.ThrowIfNull(contentBrowserState);

        contentBrowserState.SelectedFolders.Clear();

        foreach (var relativePath in GetSelectedFolders(route))
        {
            _ = contentBrowserState.SelectedFolders.Add(relativePath);
        }
    }

    internal static string BuildSelectedQuery(IEnumerable<string> selectedFolders)
    {
        ArgumentNullException.ThrowIfNull(selectedFolders);

        // Keep URL generation deterministic.
        var sortedFolders = selectedFolders.Order(StringComparer.Ordinal);
        var selectedParams = string.Join('&', sortedFolders.Select(folder => $"{SelectedQueryKey}={Uri.EscapeDataString(folder)}"));

        return string.IsNullOrEmpty(selectedParams) ? string.Empty : $"?{selectedParams}";
    }
}
