// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Routing.WinUI;

/// <summary>
/// Represents a route activator that loads view models into outlet containers within WinUI windows and views.
/// </summary>
/// <param name="loggerFactory">Logger factory to obtain an ILogger. If null, uses NullLogger.</param>
/// <remarks>
/// <para>
/// The WindowRouteActivator handles two distinct activation scenarios in WinUI applications. For
/// root routes, content is loaded directly into the window itself, requiring the window to
/// implement IOutletContainer. For child routes, content is loaded into outlets defined by parent
/// view models, creating a hierarchical structure of views and outlets.
/// </para>
/// <para>
/// During activation, the activator traverses up the route tree looking for the appropriate
/// container. For root routes, this is the window itself. For child routes, it searches parent
/// routes until it finds a view model implementing IOutletContainer. This design enables complex
/// layout structures where content can be nested at various levels within the application.
/// </para>
/// </remarks>
public sealed partial class WindowRouteActivator(ILoggerFactory? loggerFactory)
    : AbstractRouteActivator(loggerFactory)
{
    /// <summary>
    /// Activates a route by loading its view model into the appropriate outlet container.
    /// </summary>
    /// <param name="route">The route to activate, containing the view model and target outlet.</param>
    /// <param name="context">The navigation context containing the window where activation occurs.</param>
    /// <exception cref="InvalidOperationException">
    /// Thrown when no parent view model implementing IOutletContainer is found.
    /// </exception>
    /// <remarks>
    /// <para>
    /// For root routes, the content is loaded directly into the window specified by the navigation
    /// context. For child routes, the activator traverses up the route tree looking for a parent
    /// view model that implements IOutletContainer. Once found, the route's view model is loaded
    /// into the outlet specified in the route configuration within that container.
    /// </para>
    /// <para>
    /// If no suitable container is found for a child route, an <see cref="InvalidOperationException"/> is thrown. This
    /// typically indicates a mismatch between the route configuration and the actual view model hierarchy.
    /// </para>
    /// </remarks>
    protected override void DoActivateRoute(IActiveRoute route, INavigationContext context)
    {
        Debug.Assert(context is WindowNavigationContext, "expecting the router context to have been created by me");

        if (route.ViewModel is null)
        {
            return;
        }

        // Content for the activated route will be loaded in the navigation context's target for a root route,
        // and in the nearest parent that has ViewModel that is an IOutletContainer for child routes.
        var contentLoader = route.IsRoot ? context.NavigationTarget : GetNearestContentLoader(route);

        if (contentLoader is IOutletContainer outletContainer)
        {
            outletContainer.LoadContent(route.ViewModel, route.Outlet);
        }
        else
        {
            var reason = $"the {(route.IsRoot ? "window" : "parent view model")} of type '{contentLoader?.GetType().Name ?? "Unknown"}' does not implement {nameof(IOutletContainer)}.";

            LogContentLoadingError(this.Logger, route, route.Outlet, reason);

            throw new InvalidOperationException(
                $"cannot find a suitable {nameof(IOutletContainer)} for route '{route}'; {reason}");
        }

        static object? GetNearestContentLoader(IActiveRoute route)
        {
            var parent = route.Parent;

            while (parent is not null && parent.ViewModel is null)
            {
                Debug.WriteLine("Skipping parent with null ViewModel");
                parent = parent.Parent;
            }

            Debug.WriteLine($"Effective parent for '{route}' is: {parent}");

            return parent?.ViewModel;
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot load content for route '{Route}' into outlet '{Outlet}' because {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, IActiveRoute route, OutletName outlet, string because);
}
