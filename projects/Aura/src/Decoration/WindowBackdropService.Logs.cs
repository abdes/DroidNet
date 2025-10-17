// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Decoration;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
/// Logging helpers for <see cref="WindowBackdropService"/>.
/// </summary>
public sealed partial class WindowBackdropService
{
    [LoggerMessage(
        EventId = 4200,
        Level = LogLevel.Information,
        Message = "[WindowBackdrop] applying {Backdrop} backdrop to window")]
    private static partial void LogApplyingBackdrop(ILogger logger, BackdropKind backdrop);

    private void LogApplyingBackdrop(BackdropKind backdrop)
        => LogApplyingBackdrop(this.logger, backdrop);

    [LoggerMessage(
        EventId = 4201,
        Level = LogLevel.Debug,
        Message = "[WindowBackdrop] backdrop is None; skipping application")]
    private static partial void LogSkippingNoneBackdrop(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSkippingNoneBackdrop()
        => LogSkippingNoneBackdrop(this.logger);

    [LoggerMessage(
        EventId = 4202,
        Level = LogLevel.Debug,
        Message = "[WindowBackdrop] successfully applied {Backdrop} backdrop")]
    private static partial void LogBackdropApplied(ILogger logger, BackdropKind backdrop);

    [Conditional("DEBUG")]
    private void LogBackdropApplied(BackdropKind backdrop)
        => LogBackdropApplied(this.logger, backdrop);

    [LoggerMessage(
        EventId = 4203,
        Level = LogLevel.Warning,
        Message = "[WindowBackdrop] failed to apply {Backdrop} backdrop; window will continue without backdrop")]
    private static partial void LogBackdropApplicationFailed(ILogger logger, Exception exception, BackdropKind backdrop);

    private void LogBackdropApplicationFailed(Exception exception, BackdropKind backdrop)
        => LogBackdropApplicationFailed(this.logger, exception, backdrop);

    [LoggerMessage(
        EventId = 4204,
        Level = LogLevel.Information,
        Message = "[WindowBackdrop] applying backdrops to matching windows")]
    private static partial void LogApplyingBackdropsToWindows(ILogger logger);

    private void LogApplyingBackdropsToWindows()
        => LogApplyingBackdropsToWindows(this.logger);
}
