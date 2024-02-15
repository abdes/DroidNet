// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

using System.Diagnostics;
using DroidNet.Routing.Detail;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>
/// Abstract route activator to just provide an implementation for
/// <see cref="IRouteActivator.ActivateRoutesRecursive" /> using the
/// <see cref="IRouteActivator.ActivateRoute" /> method.
/// </summary>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a
/// <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" />
/// from the Dependency Injector.
/// </param>
public abstract partial class AbstractRouteActivator(
    IServiceProvider serviceProvider,
    ILoggerFactory? loggerFactory) : IRouteActivator
{
    protected ILogger Logger { get; } = loggerFactory?.CreateLogger<Router>() ??
                                        NullLoggerFactory.Instance.CreateLogger<Router>();

    /// <inheritdoc />
    public bool ActivateRoutesRecursive(IActiveRoute root, RouterContext context)
    {
        var success = true;

        // The state root is simply a placeholder for the tree and cannot be
        // activated.
        if (root.Parent != null)
        {
            success = success && this.ActivateRoute(root, context);
        }

        foreach (var route in root.Children)
        {
            success = success && this.ActivateRoutesRecursive(route, context);
        }

        return success;
    }

    /// <inheritdoc />
    public bool ActivateRoute(IActiveRoute route, RouterContext context)
    {
        var routeImpl = route as ActiveRoute ?? throw new ArgumentException(
            $"expecting the {nameof(route)} to be of the internal type {typeof(ActiveRoute)}",
            nameof(route));

        if (routeImpl.IsActivated)
        {
            LogActivationSkipped(this.Logger, route);
            return true;
        }

        Trace.TraceInformation($"Activating route `{routeImpl}` for the first time");

        // Resolve the view model and then ask the concrete activator to finish
        // activation. The ActiveRoute must be fully setup before we do the
        // actual activation, because it may be injected in the view model being
        // activated and used.
        try
        {
            // The route may be without a ViewModel (typically a route to load
            // data or hold shared parameters).
            var viewmodelType = routeImpl.RouteConfig.ViewModelType;
            if (viewmodelType != null)
            {
                var viewModel = this.ResolveViewModel(viewmodelType);
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
            LogActivationFailed(this.Logger, routeImpl, ex);
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

    private object ResolveViewModel(Type viewModelType)
    {
        var viewModel = serviceProvider.GetService(viewModelType);
        return viewModel ?? throw new MissingViewModelException(viewModelType);
    }
}
