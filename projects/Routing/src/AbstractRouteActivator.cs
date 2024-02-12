// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using DroidNet.Routing.Detail;

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

        // Resolve the view model and then ask the concrete activator to finish
        // activation. The ActiveRoute must be fully setup before we do the
        // actual activation, because it may be injected in the view model being
        // activated and used.
        try
        {
            // The route may be without a ViewModel (typically a route to load
            // data or hold shared parameters).
            var viewmodelType = route.RouteConfig.ViewModelType;
            if (viewmodelType != null)
            {
                var viewModel = this.ResolveViewModel(viewmodelType);
                ((ActiveRoute)route).ViewModel = viewModel;
                if (viewModel is IRoutingAware injectable)
                {
                    injectable.ActiveRoute = route;
                }
            }

            this.DoActivateRoute((ActiveRoute)route, context);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Route {route} activation failed: {ex}");
        }
    }

    protected abstract void DoActivateRoute(IActiveRoute route, RouterContext context);

    private object ResolveViewModel(Type viewModelType)
    {
        var viewModel = serviceProvider.GetService(viewModelType);
        return viewModel ?? throw new MissingViewModelException(viewModelType);
    }
}
