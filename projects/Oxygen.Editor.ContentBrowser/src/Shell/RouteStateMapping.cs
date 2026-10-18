// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;
using Oxygen.Managed.Core;

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
    internal static IReadOnlyList<AssetPath> ParseSelectedFoldersFromUrl(string? url)
        => string.IsNullOrEmpty(url) ? [] : ToFolders(DefaultUrlSerializer.Instance.Parse(url).QueryParams.GetValues(SelectedQueryKey));

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
    /// <returns>The selected folders.</returns>
    internal static IReadOnlyList<AssetPath> GetSelectedFolders(IActiveRoute? route)
        => ToFolders(route?.QueryParams?.GetValues(SelectedQueryKey));

    /// <summary>Encodes a deterministic folder scope for local navigation and history.</summary>
    /// <param name="selectedFolders">The selected browser folders.</param>
    /// <returns>The URL query, or an empty string for the project-wide scope.</returns>
    internal static string BuildSelectedQuery(IEnumerable<AssetPath> selectedFolders)
    {
        ArgumentNullException.ThrowIfNull(selectedFolders);

        // Keep URL generation deterministic.
        var sortedFolders = selectedFolders.Order();
        var selectedParams = string.Join('&', sortedFolders.Select(folder => $"{SelectedQueryKey}={Uri.EscapeDataString(folder.VirtualPath)}"));

        return string.IsNullOrEmpty(selectedParams) ? string.Empty : $"?{selectedParams}";
    }

    // A URL is typed by people and kept in history; values that name no folder select nothing.
    private static AssetPath[] ToFolders(IEnumerable<string?>? values)
        => [.. (values ?? []).Select(static value => AssetPath.TryParse(value, out var folder) ? folder : null).OfType<AssetPath>()];
}
