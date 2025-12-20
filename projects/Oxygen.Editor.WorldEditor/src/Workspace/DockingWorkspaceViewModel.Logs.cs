// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
///     Logging methods for <see cref="DockingWorkspaceViewModel" />.
/// </summary>
public partial class DockingWorkspaceViewModel
{
    [LoggerMessage(
    SkipEnabledCheck = true,
    Level = LogLevel.Information,
    Message = "Renderer outlet populated with ViewModel: {ViewModel}")]
    private static partial void LogRendererLoaded(ILogger logger, object viewModel);

    private void LogRendererLoaded(object viewModel)
        => LogRendererLoaded(this.logger, viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Dockable outlet `{Outlet}` populated with ViewModel: {ViewModel}")]
    private static partial void LogDockableLoaded(ILogger logger, OutletName outlet, object viewModel);

    private void LogDockableLoaded(OutletName outlet, object viewModel)
        => LogDockableLoaded(this.logger, outlet, viewModel);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Dockable with ID `{DockableId}` trying to dock relative to unknown ID `{RelativeToId}`")]
    private static partial void LogInvalidRelativeDocking(ILogger logger, string dockableId, string relativeToId);

    private void LogInvalidRelativeDocking(string dockableId, string relativeToId)
        => LogInvalidRelativeDocking(this.logger, dockableId, relativeToId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "An error occurred while loading content for route `{Path}`: {ErrorMessage}")]
    private static partial void LogDockablePlacementError(ILogger logger, string? path, string errorMessage);

    private void LogDockablePlacementError(string? path, string errorMessage)
        => LogDockablePlacementError(this.logger, path, errorMessage);
}
