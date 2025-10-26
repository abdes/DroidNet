// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="NumberBox"/>. Follows the same LoggerMessage/source-generator
///     pattern used across the project (see MenuItem.Logs.cs for the reference pattern).
///
///     Hosts can enable logging for a NumberBox instance by assigning an <see cref="ILogger"/>
///     to the internal <see cref="logger"/> property.
/// </summary>
public partial class NumberBox
{
    [LoggerMessage(EventId = 3601, Level = LogLevel.Trace, Message = "[NumberBox] Value changed from {OldValue} to {NewValue}")]
    private static partial void LogValueChanged(ILogger logger, float oldValue, float newValue);

    [Conditional("DEBUG")]
    private void LogValueChanged(float oldValue, float newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogValueChanged(logger, oldValue, newValue);
        }
    }

    [LoggerMessage(EventId = 3602, Level = LogLevel.Trace, Message = "[NumberBox] Validation changed (WasValid={OldIsValid}, NowValid={NewIsValid})")]
    private static partial void LogValidation(ILogger logger, bool oldIsValid, bool newIsValid);

    [Conditional("DEBUG")]
    private void LogValidation(bool oldIsValid, bool newIsValid)
    {
        if (this.logger is ILogger logger && oldIsValid != this.valueIsValid)
        {
            LogValidation(logger, oldIsValid, newIsValid);
        }
    }

    [LoggerMessage(EventId = 3603, Level = LogLevel.Trace, Message = "[NumberBox] Edit started (IsIndeterminate={IsIndeterminate})")]
    private static partial void LogEditStarted(ILogger logger, bool isIndeterminate);

    [Conditional("DEBUG")]
    private void LogEditStarted(bool isIndeterminate)
    {
        if (this.logger is ILogger logger)
        {
            LogEditStarted(logger, isIndeterminate);
        }
    }

    [LoggerMessage(EventId = 3604, Level = LogLevel.Trace, Message = "[NumberBox] Edit committed (NewValue={NewValue})")]
    private static partial void LogEditCommitted(ILogger logger, float newValue);

    [Conditional("DEBUG")]
    private void LogEditCommitted(float newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogEditCommitted(logger, newValue);
        }
    }

    [LoggerMessage(EventId = 3605, Level = LogLevel.Trace, Message = "[NumberBox] Edit canceled (WasIndeterminate={WasIndeterminate})")]
    private static partial void LogEditCanceled(ILogger logger, bool wasIndeterminate);

    [Conditional("DEBUG")]
    private void LogEditCanceled(bool wasIndeterminate)
    {
        if (this.logger is ILogger logger)
        {
            LogEditCanceled(logger, wasIndeterminate);
        }
    }

    [LoggerMessage(EventId = 3606, Level = LogLevel.Trace, Message = "[NumberBox] Pointer {EventType} (IsPointerOver={IsPointerOver}, IsMouseCaptured={IsMouseCaptured})")]
    private static partial void LogPointerEvent(ILogger logger, string eventType, bool isPointerOver, bool isMouseCaptured);

    [Conditional("DEBUG")]
    private void LogPointerEvent(string eventType)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerEvent(logger, eventType, this.isPointerOver, this.IsMouseCaptured);
        }
    }

    [LoggerMessage(EventId = 3607, Level = LogLevel.Trace, Message = "[NumberBox] PointerWheel delta={Delta} (IsHorizontal={IsHorizontal})")]
    private static partial void LogPointerWheel(ILogger logger, int delta, bool isHorizontal);

    [Conditional("DEBUG")]
    private void LogPointerWheel(int delta, bool isHorizontal)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerWheel(logger, delta, isHorizontal);
        }
    }

    [LoggerMessage(EventId = 3608, Level = LogLevel.Trace, Message = "[NumberBox] Key {Key} {EventType}")]
    private static partial void LogKeyEvent(ILogger logger, VirtualKey key, string eventType);

    [Conditional("DEBUG")]
    private void LogKeyEvent(VirtualKey key, string eventType)
    {
        if (this.logger is ILogger logger)
        {
            LogKeyEvent(logger, key, eventType);
        }
    }

    [LoggerMessage(EventId = 3609, Level = LogLevel.Error, Message = "[NumberBox] Commit failed")]
    private static partial void LogCommitFailed(ILogger logger, Exception exception);

    private void LogCommitFailed(Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogCommitFailed(logger, ex);
        }
    }

    [LoggerMessage(EventId = 3610, Level = LogLevel.Trace, Message = "[NumberBox] Input cursor changed to {Cursor}")]
    private static partial void LogCursorChanged(ILogger logger, string cursor);

    [Conditional("DEBUG")]
    private void LogCursorChanged(string cursor)
    {
        if (this.logger is ILogger logger)
        {
            LogCursorChanged(logger, cursor);
        }
    }
}
