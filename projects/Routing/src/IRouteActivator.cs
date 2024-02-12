// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Specifies the interface of a route activator, responsible for activating
/// routes after the router builds its router state tree out of the navigation
/// url.
/// </summary>
public interface IRouteActivator
{
    /// <summary>
    /// Activates the specified <paramref name="route" /> within the specified
    /// <paramref name="context" />.
    /// </summary>
    /// <param name="route">The route to activate.</param>
    /// <param name="context">
    /// The router context. In particular, this context will provide the target
    /// (window, content frame, etc...) the view identified by the route URL
    /// will be loaded into.
    /// </param>
    void ActivateRoute(IActiveRoute route, RouterContext context);

    /// <summary>
    /// Recursively activates a tree of <see cref="IActiveRoute" />, starting
    /// from its given  <paramref name="root" />, within the specified
    /// <paramref name="context" />.
    /// </summary>
    /// <param name="root">The root of the tree to activate.</param>
    /// <param name="context">
    /// The router context. In particular, this context will provide the target
    /// (window, content frame, etc...) the view identified by the route URL
    /// will be loaded into.
    /// </param>
    void ActivateRoutesRecursive(IActiveRoute root, RouterContext context);
}
