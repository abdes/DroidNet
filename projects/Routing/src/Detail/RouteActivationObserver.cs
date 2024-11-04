// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Detail;

using DryIoc;

/// <summary>
/// Implements the <see cref="IRouteActivationObserver" /> interface for
/// monitoring and responding to route activation lifecycle events.
/// </summary>
/// <param name="container">
/// The IoC container used to resolve ViewModel instances for the routes being activated.
/// </param>
internal sealed class RouteActivationObserver(IContainer container) : IRouteActivationObserver
{
    /// <inheritdoc />
    public bool OnActivating(IActiveRoute route, INavigationContext context)
    {
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
        var viewModelType = route.RouteConfig.ViewModelType;
        if (viewModelType is null)
        {
            return true;
        }

        var viewModel = container.GetService(viewModelType) ??
                        throw new MissingViewModelException { ViewModelType = viewModelType };
        routeImpl.ViewModel = viewModel;
        if (viewModel is IRoutingAware injectable)
        {
            injectable.ActiveRoute = routeImpl;
        }

        return true;
    }

    /// <inheritdoc />
    public void OnActivated(IActiveRoute route, INavigationContext context)
    {
        if (route is ActiveRoute routeImpl)
        {
            routeImpl.IsActivated = true;
        }
    }
}
