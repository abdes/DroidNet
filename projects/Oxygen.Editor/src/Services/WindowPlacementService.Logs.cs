// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Windowing;

namespace Oxygen.Editor.Services;

/// <summary>
///     Logging source generated methods and wrappers for the <see cref="WindowPlacementService"/>.
/// </summary>
public partial class WindowPlacementService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{ServiceName} created.")]
    private static partial void LogCreated(ILogger logger, string serviceName);

    private void LogCreated() => LogCreated(this.logger, nameof(WindowPlacementService));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Window {WindowId} has already been placed.")]
    private static partial void LogAlreadyPlaced(ILogger logger, ulong windowId);

    [Conditional("DEBUG")]
    private void LogAlreadyPlaced(ulong windowId)
        => LogAlreadyPlaced(this.logger, windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "No placement key found for window {WindowId}. Maybe routed navigation, or missing metadata.")]
    private static partial void LogNoPlacementKey(ILogger logger, ulong windowId);

    [Conditional("DEBUG")]
    private void LogNoPlacementKey(ulong windowId)
        => LogNoPlacementKey(this.logger, windowId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Restoring placement for window {WindowId} with key '{PlacementKey}'.")]
    private static partial void LogRestoringPlacement(ILogger logger, ulong windowId, string placementKey);

    [Conditional("DEBUG")]
    private void LogRestoringPlacement(ulong windowId, string placementKey)
        => LogRestoringPlacement(this.logger, windowId, placementKey);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "No placement data found for window {WindowId} with key '{PlacementKey}'.")]
    private static partial void LogNoPlacementData(ILogger logger, ulong windowId, string placementKey);

    [Conditional("DEBUG")]
    private void LogNoPlacementData(ulong windowId, string placementKey)
        => LogNoPlacementData(this.logger, windowId, placementKey);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Saving placement data for key '{PlacementKey}': {Placement}.")]
    private static partial void LogSavingPlacement(ILogger logger, string placementKey, string placement);

    [Conditional("DEBUG")]
    private void LogSavingPlacement(string key, string placement)
        => LogSavingPlacement(this.logger, key, placement);
}
