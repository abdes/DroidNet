// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using DroidNet.Routing.Detail;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>
/// Abstract route activator to just provide an implementation for <see cref="IRouteActivator.ActivateRoutesRecursive" /> using
/// the <see cref="IRouteActivator.ActivateRoute" /> method.
/// </summary>
/// <param name="container">
/// The IoC container to use when resolving a ViewModel type to a ViewModel for the route being activated.
/// </param>
/// <param name="logger">
/// The logger to be used by this base class for logging.
/// </param>
public abstract partial class AbstractRouteActivator(IContainer container, ILogger logger)
    : IRouteActivator
{
    /// <summary>
    /// Gets the logger used by this base class and its derived class.
    /// </summary>
    /// <remarks>
    /// The proper way for getting the logger is to have a <see cref="ILoggerFactory" /> injected in the concrete class, and then
    /// if it's not <see langword="null" />, get a <see cref="ILogger" /> from it, otherwise use a <see cref="NullLogger" />. This
    /// way, we ensure that if this API is used and logging is not desired, it can simply be completely turned off by providing a
    /// <see langword="null" /> <see cref="ILoggerFactory" />.
    /// </remarks>
    protected ILogger Logger => logger;

    /// <inheritdoc />
    public bool ActivateRoutesRecursive(IActiveRoute root, RouterContext context)
    {
        var activationSuccessful = true;

        // The state root is simply a placeholder for the tree and cannot be
        // activated.
        if (root.Parent != null)
        {
            activationSuccessful = activationSuccessful && this.ActivateRoute(root, context);
        }

        return root.Children.Aggregate(
            activationSuccessful,
            (currentStatus, route) => currentStatus && this.ActivateRoutesRecursive(route, context));
    }

    /// <inheritdoc />
    public bool ActivateRoute(IActiveRoute route, RouterContext context)
    {
        var routeImpl = route as ActiveRoute ?? throw new ArgumentException(
            $"expecting the {nameof(route)} to be of the internal type {typeof(ActiveRoute)}",
            nameof(route));

        if (routeImpl.IsActivated)
        {
            LogActivationSkipped(logger, route);
            return true;
        }

        Debug.WriteLine($"Activating route `{routeImpl}` for the first time");

        // Resolve the view model and then ask the concrete activator to finish
        // activation. The ActiveRoute must be fully setup before we do the
        // actual activation, because it may be injected in the view model being
        // activated and used.
        try
        {
            // The route may be without a ViewModel (typically a route to load
            // data or hold shared parameters).
            var viewModelType = routeImpl.RouteConfig.ViewModelType;
            if (viewModelType != null)
            {
                var viewModel = container.GetService(viewModelType) ??
                                throw new MissingViewModelException { ViewModelType = viewModelType };
                routeImpl.ViewModel = viewModel;
                if (viewModel is IRoutingAware injectable)
                {
                    injectable.ActiveRoute = routeImpl;
                }
            }

            this.DoActivateRoute(routeImpl, context);

            routeImpl.IsActivated = true;
            return true;
        }
        catch (Exception ex)
        {
            LogActivationFailed(logger, routeImpl, ex);
            return false;
        }
    }

    protected abstract void DoActivateRoute(IActiveRoute route, RouterContext context);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Route activation failed {Route}")]
    private static partial void LogActivationFailed(ILogger logger, IActiveRoute route, Exception ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Activation not needed because the route {Route} is already activated")]
    private static partial void LogActivationSkipped(ILogger logger, IActiveRoute route);
}
