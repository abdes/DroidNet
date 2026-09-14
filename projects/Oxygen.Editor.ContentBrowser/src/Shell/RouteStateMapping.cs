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
        => string.IsNullOrEmpty(url) ? [] : DefaultUrlSerializer.Instance.Parse(url).QueryParams.GetValues(SelectedQueryKey)?
            .OfType<string>().Where(static folder => !string.IsNullOrWhiteSpace(folder)).ToArray() ?? [];

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
