// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Specifies the interface of a route activator, responsible for activating routes after the router builds its router
/// state tree out of the navigation url.
/// </summary>
/// <remarks>
/// Route activation involves creating the ViewModel that is the subject of route navigation, optionally injecting into
/// it the <see cref="IActiveRoute" />, and finally loading its corresponding view in the visual tree. Injection of
/// <see cref="IActiveRoute" /> only happens if the ViewModel implements the <see cref="IRoutingAware" /> interface.
/// </remarks>
public interface IRouteActivator
{
    /// <summary>
    /// Activates the specified <paramref name="route" /> within the specified <paramref name="context" />.
    /// </summary>
    /// <param name="route">The route to activate.</param>
    /// <param name="context">The router context. In particular, this context will provide the target (window, content frame, etc...) the view
    /// identified by the route URL will be loaded into.
    /// </param>
    /// <returns>
    /// <see langword="true" /> if the activation was successful; <see langword="false" /> otherwise.
    /// </returns>
    bool ActivateRoute(IActiveRoute route, INavigationContext context);

    /// <summary>
    /// Recursively activates a tree of <see cref="IActiveRoute" />, starting
    /// from its given  <paramref name="root" />, within the specified <paramref name="context" />.
    /// </summary>
    /// <param name="root">The root of the tree to activate.</param>
    /// <param name="context">
    /// The router context. In particular, this context will provide the target
    /// (window, content frame, etc...) the view identified by the route URL
    /// will be loaded into.
    /// </param>
    /// <returns>
    /// <see langword="false" /> if any of the child routes activation was successful;
    /// <see langword="true" /> otherwise.
    /// </returns>
    bool ActivateRoutesRecursive(IActiveRoute root, INavigationContext context);
}
