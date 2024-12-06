// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.WorldEditor.Routing;

/// <summary>
/// Implements the <see cref="IRouteActivator" /> interface for activating routes inside a local child router.
/// </summary>
/// <param name="loggerFactory">
/// We inject a <see cref="ILoggerFactory" /> to be able to silently use a <see cref="NullLogger" /> if we fail to obtain a <see cref="ILogger" /> from the Dependency Injector.
/// </param>
[SuppressMessage("Microsoft.Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "This class is instantiated by dependency injection.")]
internal sealed partial class LocalRouteActivator(ILoggerFactory? loggerFactory)
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
            parent = parent.Parent;
        }

        var parentViewModel = parent is null ? ((ILocalRouterContext)context).RootViewModel : parent.ViewModel;
        LogEffectiveParentViewModel(this.Logger, route, parentViewModel);

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
            throw new InvalidOperationException($"parent view model of type '{route.Parent?.Config.ViewModelType} is not a {nameof(IOutletContainer)}");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot load content into {Outlet} {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, OutletName outlet, string because);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Effective parent for route '{Route}' is {ParentViewModel}")]
    private static partial void LogEffectiveParentViewModel(ILogger logger, IActiveRoute route, object? parentViewmodel);
}
