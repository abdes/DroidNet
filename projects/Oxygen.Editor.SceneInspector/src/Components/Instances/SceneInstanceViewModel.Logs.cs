// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Scenes.Edits.Instances;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>Logging methods for <see cref="SceneInstanceViewModel"/>.</summary>
public sealed partial class SceneInstanceViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Swapping an instance's source from {From} to {To}; its changes: {Swap}.")]
    private static partial void LogSwapRequested(ILogger logger, string from, string to, InstanceSwap swap);

    private void LogSwapRequested(AssetPath from, AssetPath to, InstanceSwap swap)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogSwapRequested(this.logger, from.ToString(), to.ToString(), swap);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The instance's source was not swapped: {Code}.")]
    private static partial void LogSwapRefused(ILogger logger, string code);

    private void LogSwapRefused(string? code)
    {
        if (this.logger.IsEnabled(LogLevel.Warning))
        {
            LogSwapRefused(this.logger, code ?? "no diagnostic");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared content catalog feed failed; the instance source picker stops following it.")]
    private static partial void LogCatalogFeedFailed(ILogger logger, Exception exception);

    private void LogCatalogFeedFailed(Exception exception) => LogCatalogFeedFailed(this.logger, exception);
}
