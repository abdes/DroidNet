// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>
///   Logging methods for <see cref="TransformEditController"/>.
/// </summary>
internal sealed partial class TransformEditController
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[TransformViewModel] Constructed (HasLoggerFactory={HasFactory})")]
    private static partial void LogConstructed(ILogger logger, bool hasFactory);

    [Conditional("DEBUG")]
    private void LogConstructed()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var hasFactory = this.LoggerFactory is not null;
        LogConstructed(this.logger, hasFactory);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[TransformViewModel] Updating values for {Count} selected items")]
    private static partial void LogUpdateValues(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogUpdateValues(int count)
        => LogUpdateValues(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[TransformViewModel] Applying change: {Property} = {Value} to {TargetCount} items")]
    private static partial void LogApplyingChange(ILogger logger, string property, float value, int targetCount);

    [Conditional("DEBUG")]
    private void LogApplyingChange(string property, float value, int targetCount)
        => LogApplyingChange(this.logger, property, value, targetCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "[TransformViewModel] Failed to apply change to property {Property}.")]
    private static partial void LogApplyFailed(ILogger logger, string property, Exception exception);

    private void LogApplyFailed(string property, Exception ex)
        => LogApplyFailed(this.logger, property, ex);
}
