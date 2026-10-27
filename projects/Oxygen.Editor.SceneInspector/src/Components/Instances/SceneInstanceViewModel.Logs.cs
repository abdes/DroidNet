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
    private static partial void LogSwapRequested(ILogger logger, AssetPath from, AssetPath to, InstanceSwap swap);

    private void LogSwapRequested(AssetPath from, AssetPath to, InstanceSwap swap)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogSwapRequested(this.logger, from, to, swap);
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
        Level = LogLevel.Information,
        Message = "Unpacking the instance ({Depth}).")]
    private static partial void LogUnpackRequested(ILogger logger, InstanceUnpack depth);

    private void LogUnpackRequested(InstanceUnpack depth)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogUnpackRequested(this.logger, depth);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The instance was not unpacked: {Code}.")]
    private static partial void LogUnpackRefused(ILogger logger, string code);

    private void LogUnpackRefused(string? code)
    {
        if (this.logger.IsEnabled(LogLevel.Warning))
        {
            LogUnpackRefused(this.logger, code ?? "no diagnostic");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Applying the instance's changes to {Scene}.")]
    private static partial void LogApplyRequested(ILogger logger, AssetPath scene);

    private void LogApplyRequested(AssetPath scene)
    {
        if (this.logger.IsEnabled(LogLevel.Information))
        {
            LogApplyRequested(this.logger, scene);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The instance's changes were not applied: {Code}.")]
    private static partial void LogApplyRefused(ILogger logger, string code);

    private void LogApplyRefused(string? code)
    {
        if (this.logger.IsEnabled(LogLevel.Warning))
        {
            LogApplyRefused(this.logger, code ?? "no diagnostic");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared content catalog feed failed; the instance source picker stops following it.")]
    private static partial void LogCatalogFeedFailed(ILogger logger, Exception exception);

    private void LogCatalogFeedFailed(Exception exception) => LogCatalogFeedFailed(this.logger, exception);
}
