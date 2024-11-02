// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.WinUI;

using System.Diagnostics;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

/// <summary>
/// Represents a route activator (<see cref="IRouteActivator" />) that activates routes by loading their view models into <see cref="IOutletContainer">outlets</see> defined in their parent views.
/// </summary>
/// <remarks>
/// If the route being activated is the root route, then it does not have a parent. In such case, the content is loaded into the
/// context's window, provided that window implements <see cref="IOutletContainer" />.
/// </remarks>
/// <param name="container">
/// The IoC container to use when resolving a ViewModel type to a ViewModel for the route being activated.
/// </param>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" /> from the Dependency Injector.
/// </param>
internal sealed partial class WindowRouteActivator(
    IContainer container,
    ILoggerFactory? loggerFactory) : AbstractRouteActivator(container, CreateLogger(loggerFactory))
{
    /// <inheritdoc />
    protected override void DoActivateRoute(IActiveRoute route, RouterContext context)
    {
        Debug.Assert(context is WindowRouterContext, "expecting the router context to have been created by me");
        Debug.Assert(route.Parent is not null, "you cannot activate the root!");

        if (route.ViewModel is null)
        {
            return;
        }

        /*
         * For a root route, load the content in the context's window outlet.
         * For a normal route, load the content in the corresponding outlet in
         * its parent activated route view model.
         */

        if (route.Parent.IsRoot)
        {
            this.DoActivateRootRoute(route, context);
        }
        else
        {
            // Go up the tree to find the first parent, skipping the routes
            // with no view models in between.
            var parent = route.Parent;
            while (parent is not null && parent.ViewModel is null)
            {
                Debug.WriteLine("Skipping parent with null ViewModel");
                parent = parent.Parent;
            }

            Debug.WriteLine($"Effective parent for '{route}' is: {parent}");

            if (parent?.ViewModel is IOutletContainer outletContainer)
            {
                outletContainer.LoadContent(route.ViewModel, route.Outlet);
            }
            else
            {
                var because = parent is null
                    ? $"I could not find an {nameof(IOutletContainer)} parent for it"
                    : $"the view model of its parent ({parent.ViewModel}) is not an {nameof(IOutletContainer)}";
                LogContentLoadingError(this.Logger, route.Outlet, because);

                throw new InvalidOperationException(
                    $"parent view model of type '{route.Parent.RouteConfig.ViewModelType} is not a {nameof(IOutletContainer)}");
            }
        }
    }

    private static ILogger<WindowRouteActivator> CreateLogger(ILoggerFactory? loggerFactory)
        => loggerFactory?.CreateLogger<WindowRouteActivator>() ??
           NullLoggerFactory.Instance.CreateLogger<WindowRouteActivator>();

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot load content into {Outlet} {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, OutletName outlet, string because);

    private void DoActivateRootRoute(IActiveRoute route, RouterContext context)
    {
        Debug.Assert(route.ViewModel is not null, "expecting the view model not to be null");
        var window = (context as WindowRouterContext)?.Window;
        Debug.Assert(
            window is not null,
            "expecting the window in the router context to be created by me");

        if (window is IOutletContainer outletContainer)
        {
            outletContainer.LoadContent(route.ViewModel, route.Outlet);
        }
        else
        {
            LogContentLoadingError(
                this.Logger,
                route.Outlet,
                $"because {window.GetType()} is not an {nameof(IOutletContainer)}");

            throw new InvalidOperationException(
                $"target window of type '{window.GetType()} is not a {nameof(IOutletContainer)}");
        }
    }
}
