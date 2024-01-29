// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using DroidNet.Routing.Contracts;

/// <summary>
/// Abstract route activator to just provide an implementation for
/// <see cref="IRouteActivator.ActivateRoutesRecursive" /> using the
/// <see cref="IRouteActivator.ActivateRoute" /> method.
/// </summary>
public abstract class AbstractRouteActivator(IServiceProvider serviceProvider) : IRouteActivator
{
    /// <inheritdoc />
    public void ActivateRoutesRecursive(IActiveRoute root, RouterContext context)
    {
        // The state root is simply a placeholder for the tree and cannot be
        // activated.
        if (root.Parent != null)
        {
            this.ActivateRoute(root, context);
        }

        foreach (var route in root.Children)
        {
            this.ActivateRoutesRecursive(route, context);
        }
    }

    /// <inheritdoc />
    public void ActivateRoute(IActiveRoute route, RouterContext context)
    {
        Debug.Assert(
            route is ActiveRoute,
            $"expecting the {nameof(route)} to be of the internal type {typeof(ActiveRoute)}");

        try
        {
            ((ActiveRoute)route).ViewModel = this.DoActivateRoute((ActiveRoute)route, context);

            if (route.ViewModel is IRoutingAware routeViewModel)
            {
                routeViewModel.ActiveRoute = route;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Route {route} activation failed: {ex}");
        }
    }

    protected object ResolveViewModel(Type viewModelType)
    {
        var viewModel = serviceProvider.GetService(viewModelType);
        return viewModel ?? throw new MissingViewModelException(viewModelType);
    }

    protected abstract object? DoActivateRoute(IActiveRoute route, RouterContext context);
}
