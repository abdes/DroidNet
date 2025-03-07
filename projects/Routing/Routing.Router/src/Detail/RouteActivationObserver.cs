// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;

namespace DroidNet.Routing.Detail;

/// <summary>
/// Implements the <see cref="IRouteActivationObserver"/> interface for monitoring and responding to route activation lifecycle events.
/// </summary>
/// <param name="container">
/// The IoC container used to resolve ViewModel instances for the routes being activated.
/// </param>
/// <remarks>
/// The <see cref="RouteActivationObserver"/> class is responsible for observing the activation
/// process of routes within the routing system. It ensures that view models are properly
/// instantiated and injected into the routes being activated, and that the activated route is
/// injected into the instantiated view model if it implements the <see cref="IRoutingAware"/>
/// interface.
/// <para>
/// This class interacts with the IoC container to resolve view model instances and handles the
/// lifecycle events of route activation, such as pre-activation checks and post-activation
/// notifications.
/// </para>
/// </remarks>
internal sealed class RouteActivationObserver(IContainer container) : IRouteActivationObserver
{
    /// <summary>
    /// Called before a route is activated to perform any necessary pre-activation logic.
    /// </summary>
    /// <param name="route">The route that is about to be activated.</param>
    /// <param name="context">The navigation context in which the route is being activated.</param>
    /// <returns>
    /// <see langword="true"/> if the route should proceed with activation; otherwise, <see langword="false"/>.
    /// </returns>
    /// <exception cref="ArgumentException">
    /// Thrown if the provided route is not of type <see cref="ActiveRoute"/>.
    /// </exception>
    /// <exception cref="MissingViewModelException">
    /// Thrown if the view model for the route cannot be resolved from the container.
    /// </exception>
    /// <remarks>
    /// This method performs several checks and preparations before a route is activated. It
    /// verifies that the route is of the correct type and that it has not already been activated.
    /// If the route requires a view model, the method resolves the view model from the IoC
    /// container and assigns it to the route.
    /// </remarks>
    public bool OnActivating(IActiveRoute route, INavigationContext context)
    {
        _ = context; // Unused

        if (route is not ActiveRoute routeImpl)
        {
            throw new ArgumentException(
                $"Expected route `{nameof(route)}` to be of type `{typeof(ActiveRoute)}`.",
                nameof(route));
        }

        if (routeImpl.IsActivated)
        {
            return false;
        }

        // The route may be without a ViewModel (typically a route to load
        // data or hold shared parameters).
        var viewModelType = route.Config.ViewModelType;
        if (viewModelType is null)
        {
            return true;
        }

        var viewModel = container.Resolve(viewModelType) ??
            throw new MissingViewModelException { ViewModelType = viewModelType };
        routeImpl.ViewModel = viewModel;

        return true;
    }

    /// <summary>
    /// Called after a route has been successfully activated to perform any necessary post-activation logic.
    /// </summary>
    /// <param name="route">The route that has been activated.</param>
    /// <param name="context">The navigation context in which the route was activated.</param>
    /// <remarks>
    /// This method is called after a route has been successfully activated. It sets the
    /// <see cref="ActiveRoute.IsActivated"/> property to <see langword="true"/>, indicating that the
    /// route is now active. This method ensures that any additional logic that needs to be executed
    /// after activation is performed, such as updating the state of the route or notifying other
    /// components of the activation.
    /// <para>
    /// If the view model implements <see cref="IRoutingAware"/>, its <see cref="IRoutingAware.OnNavigatedToAsync(IActiveRoute, INavigationContext)"/>
    /// method is invoked, providing the view model with the data inside the active route, including
    /// the route's parameters, and the navigation context.
    /// </para>
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public async Task OnActivatedAsync(IActiveRoute route, INavigationContext context)
    {
        _ = context; // Unused

        if (route.ViewModel is IRoutingAware routingAware)
        {
            // Provide the activated route as well as the context to the view model, so that the
            // view model does not have to listen for router events, to get access to the context,
            // if it does not need to.
            await routingAware.OnNavigatedToAsync(route, context).ConfigureAwait(true);
        }

        if (route is ActiveRoute routeImpl)
        {
            routeImpl.IsActivated = true;
        }
    }
}
