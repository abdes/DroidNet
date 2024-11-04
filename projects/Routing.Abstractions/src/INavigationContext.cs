// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents the context in which a navigation is taking place, which includes the navigation <see cref="Target" />,
/// and the <see cref="IRouterState">router state</see>.
/// <remarks>
/// The navigation context is created by a <see cref="IContextProvider">context factory</see>, which implements the
/// <see cref="IContextProvider" /> interface. It is common that the application needs additional data and services
/// available in the navigation context, and implements an extended <see cref="INavigationContext" /> for such purpose.
/// </remarks>
/// </summary>
public interface INavigationContext
{
    /// <summary>
    /// Gets the name of the navigation target where the root content should be
    /// loaded.
    /// </summary>
    /// <value>
    /// The name of the navigation target where the root content should be
    /// loaded.
    /// </value>
    Target Target { get; }

    /// <summary>
    /// Gets the <see cref="IRouterState">router state</see> for this navigation context.
    /// </summary>
    IRouterState? State { get; }

    /// <summary>
    /// Gets the observer for route activation lifecycle events.
    /// </summary>
    IRouteActivationObserver? RouteActivationObserver { get; }
}
