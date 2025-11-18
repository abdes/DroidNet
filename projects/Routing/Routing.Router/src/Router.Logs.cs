// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;

namespace DroidNet.Routing;

/// <inheritdoc cref="IRouter" />
public sealed partial class Router
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route optimization: no state available")]
    private static partial void LogOptimizeRouteActivationNoState(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOptimizeRouteActivationNoState()
        => LogOptimizeRouteActivationNoState(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route optimization: no previous state, skipping optimization")]
    private static partial void LogOptimizeRouteActivationNoPreviousState(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOptimizeRouteActivationNoPreviousState()
        => LogOptimizeRouteActivationNoPreviousState(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Optimizating route activation for root path: {RootPath}")]
    private static partial void LogOptimizeRouteActivation(ILogger logger, string rootPath);

    private void LogOptimizeRouteActivation(IActiveRoute rootNode)
        => LogOptimizeRouteActivation(this.logger, rootNode.Config.Path ?? "root");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route reuse detected: path={RoutePath}, outlet={OutletName}")]
    private static partial void LogRouteReuseDetected(ILogger logger, string routePath, string outletName);

    [Conditional("DEBUG")]
    private void LogRouteReuseDetected(IActiveRoute route)
        => LogRouteReuseDetected(this.logger, route.Config.Path ?? "root", route.Outlet?.Name ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route not reusable: path={RoutePath}, outlet={OutletName}")]
    private static partial void LogRouteNotReusable(ILogger logger, string routePath, string outletName);

    [Conditional("DEBUG")]
    private void LogRouteNotReusable(IActiveRoute route)
        => LogRouteNotReusable(this.logger, route.Config.Path ?? "root", route.Outlet?.Name ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Route has no match in previous state: path={RoutePath}, outlet={OutletName}")]
    private static partial void LogRouteNoMatch(ILogger logger, string routePath, string outletName);

    [Conditional("DEBUG")]
    private void LogRouteNoMatch(IActiveRoute route)
        => LogRouteNoMatch(this.logger, route.Config.Path ?? "root", route.Outlet?.Name ?? "<unknown>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{RouterEvent}")]
    private static partial void LogRouterEvent(ILogger logger, RouterEvent routerEvent);

    private void LogRouterEvent(RouterEvent routerEvent)
        => LogRouterEvent(this.logger, routerEvent);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Navigation failed!")]
    private static partial void LogNavigationFailed(ILogger logger, Exception ex);

    private void LogNavigationFailed(Exception ex)
        => LogNavigationFailed(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Disposing routes for destroyed context '{TargetName}'")]
    private static partial void LogContextCleanupStarted(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogContextCleanupStarted(string targetName)
        => LogContextCleanupStarted(this.logger, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Finished disposing routes for context '{TargetName}'")]
    private static partial void LogContextCleanupCompleted(ILogger logger, string targetName);

    [Conditional("DEBUG")]
    private void LogContextCleanupCompleted(string targetName)
        => LogContextCleanupCompleted(this.logger, targetName);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to dispose routes for context '{TargetName}'")]
    private static partial void LogContextCleanupFailed(ILogger logger, string targetName, Exception ex);

    private void LogContextCleanupFailed(string targetName, Exception ex)
        => LogContextCleanupFailed(this.logger, targetName, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Disposing ViewModel for route '{Route}' threw an exception")]
    private static partial void LogViewModelDisposeFailed(ILogger logger, IActiveRoute route, Exception ex);

    private void LogViewModelDisposeFailed(IActiveRoute route, Exception ex)
        => LogViewModelDisposeFailed(this.logger, route, ex);
}
