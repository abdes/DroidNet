// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors of two, three or
///     four components.
/// </summary>
public partial class VectorBox
{
    [LoggerMessage(EventId = 3620, Level = LogLevel.Error, Message = "[VectorBox] Exception: {Message}")]
    private static partial void LogException(ILogger logger, string message, Exception exception);

    private void LogException(Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogException(logger, ex.Message, ex);
        }
    }

    [LoggerMessage(EventId = 3622, Level = LogLevel.Trace, Message = "[VectorBox] Sync start")]
    private static partial void LogSyncStart(ILogger logger);

    private void LogSyncStart()
    {
        if (this.logger is ILogger logger)
        {
            LogSyncStart(logger);
        }
    }

    [LoggerMessage(EventId = 3623, Level = LogLevel.Trace, Message = "[VectorBox] Sync end")]
    private static partial void LogSyncEnd(ILogger logger);

    private void LogSyncEnd()
    {
        if (this.logger is ILogger logger)
        {
            LogSyncEnd(logger);
        }
    }

    [LoggerMessage(EventId = 3621, Level = LogLevel.Trace, Message = "[VectorBox] Component {Component} value changed from {OldValue} to {NewValue}")]
    private static partial void LogComponentChanged(ILogger logger, string component, double oldValue, double newValue);

    private void LogComponentChanged(string component, double oldValue, double newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogComponentChanged(logger, component, oldValue, newValue);
        }
    }

    [LoggerMessage(EventId = 3624, Level = LogLevel.Trace, Message = "[VectorBox] NumberBox for component {Component} attached")]
    private static partial void LogNumberBoxAttached(ILogger logger, string component);

    private void LogNumberBoxAttached(string component)
    {
        if (this.logger is ILogger logger)
        {
            LogNumberBoxAttached(logger, component);
        }
    }

    [LoggerMessage(EventId = 3625, Level = LogLevel.Trace, Message = "[VectorBox] Validation relayed for {Component} (Old={OldValue}, New={NewValue}, IsValid={IsValid})")]
    private static partial void LogValidationRelayed(ILogger logger, string component, double oldValue, double newValue, bool isValid);

    private void LogValidationRelayed(string component, double oldValue, double newValue, bool isValid)
    {
        if (this.logger is ILogger logger)
        {
            LogValidationRelayed(logger, component, oldValue, newValue, isValid);
        }
    }

    [LoggerMessage(EventId = 3701, Level = LogLevel.Trace, Message = "[VectorBox] SetValues called: Count={Count}, PreserveIndeterminate={Preserve}")]
    private static partial void LogSetValues(ILogger logger, int count, bool preserve);

    [Conditional("DEBUG")]
    private void LogSetValues(int count, bool preserve)
    {
        if (this.logger is ILogger logger)
        {
            LogSetValues(logger, count, preserve);
        }
    }

    [LoggerMessage(EventId = 3707, Level = LogLevel.Trace, Message = "[VectorBox] OnApplyTemplate invoked and parts applied")]
    private static partial void LogApplyTemplate(ILogger logger);

    [Conditional("DEBUG")]
    private void LogApplyTemplate()
    {
        if (this.logger is ILogger logger)
        {
            LogApplyTemplate(logger);
        }
    }

    [LoggerMessage(EventId = 3708, Level = LogLevel.Trace, Message = "[VectorBox] LoggerFactory changed (HasFactory={HasFactory})")]
    private static partial void LogLoggerFactoryChanged(ILogger logger, bool hasFactory);

    [Conditional("DEBUG")]
    private void LogLoggerFactoryChanged(bool hasFactory)
    {
        if (this.logger is ILogger logger)
        {
            LogLoggerFactoryChanged(logger, hasFactory);
        }
    }
}
