// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Detail;

namespace DroidNet.Routing;

/// <inheritdoc cref="IRouter" />
public sealed partial class Router
{
    /// <summary>
    /// Determines whether a new route can be reused from a previous route.
    /// </summary>
    /// <param name="newRoute">The route from the new state.</param>
    /// <param name="previousRoute">The route from the previous state.</param>
    /// <returns>True if the routes can be reused; otherwise, false.</returns>
    private static bool CanReuseRoute(IActiveRoute newRoute, IActiveRoute previousRoute)
    {
        // Routes can be reused if:
        // 1. They have the same configuration (same path, same view model type)
        // 2. They have the same parameters
        // 3. They have the same query parameters
        // 4. The previous route was already activated
        if (previousRoute is not ActiveRoute previousRouteImpl || !previousRouteImpl.IsActivated)
        {
            return false;
        }

        // Check if configurations match
        var newPath = newRoute.Config.Path ?? string.Empty;
        var prevPath = previousRoute.Config.Path ?? string.Empty;
        if (!newPath.Equals(prevPath, StringComparison.Ordinal) ||
            newRoute.Config.ViewModelType != previousRoute.Config.ViewModelType)
        {
            return false;
        }

        // Check if parameters match
        if (!newRoute.Params.Equals(previousRoute.Params))
        {
            return false;
        }

        // Check if query parameters match
        return newRoute.QueryParams.Equals(previousRoute.QueryParams);
    }

    /// <summary>
    /// Optimizes the activation of routes in the given context.
    /// </summary>
    /// <param name="context">The router context in which to optimize route activation.</param>
    /// <param name="previousState">The previous router state, or null if this is the first navigation.</param>
    /// <remarks>
    /// This method is intended to improve the efficiency of route activation by reusing previously
    /// activated routes in the router state. It traverses the new route tree and marks routes that
    /// match previously activated routes in the old state as already activated, preventing unnecessary
    /// re-activation of unchanged routes. Optimization is only applied if the route structures are
    /// compatible (same structure), preventing reuse of stale state from previous window sessions.
    /// </remarks>
    private void OptimizeRouteActivation(NavigationContext context, IRouterState? previousState)
    {
        if (context.State is null)
        {
            this.LogOptimizeRouteActivationNoState();
            return;
        }

        if (previousState is null)
        {
            this.LogOptimizeRouteActivationNoPreviousState();
            return;
        }

        this.LogOptimizeRouteActivation(context.State.RootNode);
        this.MarkAlreadyActivatedRoutes(context.State.RootNode, previousState.RootNode);
    }

    /// <summary>
    /// Recursively marks routes as already activated by matching them with a previous state.
    /// </summary>
    /// <param name="newRoute">The route from the new state to check.</param>
    /// <param name="previousRoute">The corresponding route from the previous state, or null if not found.</param>
    private void MarkAlreadyActivatedRoutes(IActiveRoute newRoute, IActiveRoute? previousRoute)
    {
        if (previousRoute is not null && CanReuseRoute(newRoute, previousRoute))
        {
            this.LogRouteReuseDetected(newRoute);

            if (previousRoute is ActiveRoute previousRouteImpl && newRoute is ActiveRoute newRouteImpl)
            {
                newRouteImpl.ViewModel = previousRouteImpl.ViewModel;
                newRouteImpl.IsActivated = true;
            }
        }
        else if (previousRoute is not null)
        {
            this.LogRouteNotReusable(newRoute);
        }
        else
        {
            this.LogRouteNoMatch(newRoute);
        }

        // Recursively process children
        // NOTE: A parent route may be deemed non-reusable while one or more of its
        // children remain reusable. We still attempt to reuse those children so that
        // stable nested outlets (tool panes, sidebars, etc.) keep their state even when
        // the parent container layout changes.
        foreach (var newChild in newRoute.Children)
        {
            IActiveRoute? matchingPreviousChild = null;

            if (previousRoute is not null)
            {
                // Try to find a matching child in the previous route by outlet name
                matchingPreviousChild = previousRoute.Children.FirstOrDefault(
                    c => c.Outlet.Name.Equals(newChild.Outlet.Name, StringComparison.Ordinal));
            }

            this.MarkAlreadyActivatedRoutes(newChild, matchingPreviousChild);
        }
    }
}
