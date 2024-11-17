// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT


using System.Diagnostics;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.WorldEditor.ContentBrowser.Routing;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;
/// <summary>
/// Implements the <see cref="IRouteActivator" /> interface for activating routes inside the internal router used by the Content
/// Browser module.
/// </summary>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" /> from the Dependency Injector.
/// </param>
internal sealed partial class InternalRouteActivator(ILoggerFactory? loggerFactory)
    : AbstractRouteActivator(loggerFactory)
{
    /// <inheritdoc/>
    protected override void DoActivateRoute(IActiveRoute route, INavigationContext context)
    {
        if (route.ViewModel is null)
        {
            return;
        }

        /*
         * Load the content in the corresponding outlet in its parent activated route view model.
         */

        // Go up the tree to find the first parent, skipping the routes with no view models in between.
        var parent = route.Parent;
        while (parent is not null && parent.ViewModel is null)
        {
            Debug.WriteLine("Skipping parent with null ViewModel");
            parent = parent.Parent;
        }

        var parentViewModel = parent is null ? ((ILocalRouterContext)context).RootViewModel : parent.ViewModel;

        Debug.WriteLine($"Effective parent ViewModel for '{route}' is: {parentViewModel}");

        if (parentViewModel is IOutletContainer outletContainer)
        {
            outletContainer.LoadContent(route.ViewModel, route.Outlet);
        }
        else
        {
            var because = parent is null
                ? $"I could not find an {nameof(IOutletContainer)} parent for it"
                : $"the view model of its parent ({parentViewModel}) is not an {nameof(IOutletContainer)}";
            LogContentLoadingError(this.Logger, route.Outlet, because);

            throw new InvalidOperationException(
                $"parent view model of type '{route.Parent?.Config.ViewModelType} is not a {nameof(IOutletContainer)}");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot load content into {Outlet} {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, OutletName outlet, string because);
}
