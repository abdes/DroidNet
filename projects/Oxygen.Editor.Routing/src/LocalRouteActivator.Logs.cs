// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Routing;

/// <summary>Logging helpers for <see cref="LocalRouteActivator"/>.</summary>
internal sealed partial class LocalRouteActivator
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot load content into {Outlet} {Because}")]
    private static partial void LogContentLoadingError(ILogger logger, OutletName outlet, string because);

    private void LogContentLoadingError(OutletName outlet, object? parentViewModel, bool hasParent)
    {
        if (!this.Logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var because = hasParent
            ? $"the view model of its parent ({parentViewModel}) is not an {nameof(IOutletContainer)}"
            : $"I could not find an {nameof(IOutletContainer)} parent for it";
        LogContentLoadingError(this.Logger, outlet, because);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Effective parent for route '{Route}' is {ParentViewModel}")]
    private static partial void LogEffectiveParentViewModel(ILogger logger, IActiveRoute route, object? parentViewmodel);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogEffectiveParentViewModel(IActiveRoute route, object? parentViewModel)
        => LogEffectiveParentViewModel(this.Logger, route, parentViewModel);
}
