// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;
using DroidNet.Routing;

namespace Oxygen.Editor.Shell.Layout;

/// <summary>
///     The panels closed in a workspace window, and where they were docked, so that showing one again docks it back.
/// </summary>
/// <param name="router">The window's router, whose routes are the window's panels.</param>
/// <param name="context">The window's navigation context, whose state holds the panel routes.</param>
/// <param name="docker">The window's docker.</param>
/// <remarks>
///     Closing a panel closes its dock and deletes its route; its view model is retained by the window and comes back
///     with the panel. A panel shown again goes back to its former dock when another of that dock's panels is still
///     shown, else where that dock was anchored. A panel the window never showed docks with the default anchor.
/// </remarks>
public sealed class ClosedPanels(IRouter router, INavigationContext context, IDocker docker)
{
    private const string With = "with";

    private readonly Dictionary<string, Placement> placements = new(StringComparer.Ordinal);

    /// <summary>Remembers where the panels of closed docks were, before their routes are deleted.</summary>
    /// <param name="closed">The routes of the closed panels, by outlet, which is the panel's identity.</param>
    public void Remember(IReadOnlyCollection<IActiveRoute> closed)
    {
        ArgumentNullException.ThrowIfNull(closed);
        var parametersOf = closed.ToDictionary(static route => route.Outlet.Name, static route => route.Params, StringComparer.Ordinal);
        foreach (var (panelId, parameters) in parametersOf)
        {
            // A panel tabbed with another is anchored 'with' it; the dock's own anchor is on the panel that was
            // active, or the dock is the one of a panel that did not close.
            var dock = panelId;
            var anchor = parameters;
            var visited = new HashSet<string>(StringComparer.Ordinal) { panelId };
            while (MateOf(anchor) is { } mate)
            {
                dock = mate;
                if (!parametersOf.TryGetValue(mate, out var mateParameters) || !visited.Add(mate))
                {
                    break;
                }

                anchor = mateParameters;
            }

            this.placements[panelId] = new Placement(dock, Copy(anchor));
        }
    }

    /// <summary>Shows a closed panel again, where it was, with the view model the window retained.</summary>
    /// <param name="panelId">The panel's identity, which is the outlet of its route.</param>
    /// <param name="viewModelType">The type of the panel's view model.</param>
    /// <returns><see langword="true"/> when the panel is shown.</returns>
    public async Task<bool> ShowAsync(string panelId, Type viewModelType)
    {
        ArgumentException.ThrowIfNullOrEmpty(panelId);
        ArgumentNullException.ThrowIfNull(viewModelType);
        if (context.State?.RootNode is not { } windowRoute)
        {
            return false;
        }

        await router.NavigateAsync(
            [
                new RouteChangeItem
                {
                    ChangeAction = RouteChangeAction.Add,
                    Outlet = panelId,
                    ViewModelType = viewModelType,
                    Parameters = this.PlacementOf(panelId),
                },
            ],
            new PartialNavigation { RelativeTo = windowRoute }).ConfigureAwait(true);
        return docker.FindDockable(panelId) is not null;
    }

    private static string? MateOf(IParameters anchor) => anchor.TryGetValue(With, out var mate) ? mate : null;

    private static Parameters Copy(IParameters parameters, string? except = null)
    {
        var copy = new Parameters();
        foreach (var parameter in parameters.Where(parameter => !string.Equals(parameter.Name, except, StringComparison.Ordinal)))
        {
            copy.AddOrUpdate(parameter.Name, parameter.Value);
        }

        return copy;
    }

    // The route parameters that dock a closed panel back where it was; empty, for the default anchor, when the window
    // never showed the panel.
    private IParameters PlacementOf(string panelId) => this.PlacementOf(panelId, new HashSet<string>(StringComparer.Ordinal) { panelId });

    private IParameters PlacementOf(string panelId, HashSet<string> visited)
    {
        if (!this.placements.TryGetValue(panelId, out var placement))
        {
            return new Parameters();
        }

        // Back in the former dock, when one of its panels is shown.
        var shownMate = this.IsShown(placement.Dock)
            ? placement.Dock
            : this.placements.FirstOrDefault(other => !string.Equals(other.Key, panelId, StringComparison.Ordinal)
                && string.Equals(other.Value.Dock, placement.Dock, StringComparison.Ordinal) && this.IsShown(other.Key)).Key;
        if (shownMate is not null)
        {
            var tabbed = new Parameters();
            tabbed.AddOrUpdate(With, shownMate);
            return tabbed;
        }

        // Else where the dock was: its anchor, or, for the dock of a panel that closed later, that panel's placement.
        if (MateOf(placement.Anchor) is not { } mate)
        {
            return placement.Anchor;
        }

        return visited.Add(mate) && this.placements.ContainsKey(mate) ? this.PlacementOf(mate, visited) : Copy(placement.Anchor, except: With);
    }

    private bool IsShown(string panelId) => docker.FindDockable(panelId) is not null;

    // The panel that held the anchor of the panel's dock, and that anchor's parameters.
    private sealed record Placement(string Dock, IParameters Anchor);
}
