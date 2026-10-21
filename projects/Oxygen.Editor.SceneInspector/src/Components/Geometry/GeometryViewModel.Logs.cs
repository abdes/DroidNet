// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>Logging methods for <see cref="GeometryViewModel"/>.</summary>
public sealed partial class GeometryViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared asset feed failed; the geometry and material pickers stop following it.")]
    private static partial void LogAssetFeedFailed(ILogger logger, Exception exception);

    private void LogAssetFeedFailed(Exception exception) => LogAssetFeedFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared asset catalog could not be refreshed for the geometry inspector.")]
    private static partial void LogAssetRefreshFailed(ILogger logger, Exception exception);

    private void LogAssetRefreshFailed(Exception exception) => LogAssetRefreshFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "A geometry inspector update could not reach the UI thread.")]
    private static partial void LogDispatchFailed(ILogger logger, Exception exception);

    private void LogDispatchFailed(Exception exception) => LogDispatchFailed(this.logger, exception);
}
