// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Defines an observer interface for monitoring and responding to route activation lifecycle events. Allows the router
/// to manage the activation internal logic, while keeping most of the implementation testable.
/// </summary>
public interface IRouteActivationObserver
{
    /// <summary>
    /// Invoked before attempting to activate a route. Determines if the route activation should proceed based on the
    /// current state and conditions.
    /// </summary>
    /// <param name="route">The route that is being considered for activation.</param>
    /// <param name="context">The navigation context associated with the activation.</param>
    /// <returns>
    /// <see langword="true" /> if activation should proceed; otherwise, <see langword="false" /> to cancel activation.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// Thrown if <paramref name="route" /> is not of a valid or expected type.
    /// </exception>
    bool OnActivating(IActiveRoute route, INavigationContext context);

    /// <summary>
    /// Invoked after a route has been successfully activated.
    /// Allows the observer to perform any required post-activation updates or operations.
    /// </summary>
    /// <param name="route">The route that has been activated.</param>
    /// <param name="context">The navigation context associated with the activation.</param>
    void OnActivated(IActiveRoute route, INavigationContext context);
}
