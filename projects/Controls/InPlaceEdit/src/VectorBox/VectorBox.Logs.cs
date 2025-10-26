// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric
///     vectors (Vector2/Vector3).
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

    [LoggerMessage(EventId = 3622, Level = LogLevel.Trace, Message = "[VectorBox] Component {Component} value changed from {OldValue} to {NewValue}")]
    private static partial void LogComponentChanged(ILogger logger, string component, float oldValue, float newValue);

    private void LogComponentChanged(string component, float oldValue, float newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogComponentChanged(logger, component, oldValue, newValue);
        }
    }

    [LoggerMessage(EventId = 3623, Level = LogLevel.Trace, Message = "[VectorBox] NumberBox for component {Component} attached")]
    private static partial void LogNumberBoxAttached(ILogger logger, string component);

    private void LogNumberBoxAttached(string component)
    {
        if (this.logger is ILogger logger)
        {
            LogNumberBoxAttached(logger, component);
        }
    }

    [LoggerMessage(EventId = 3625, Level = LogLevel.Trace, Message = "[VectorBox] Validation relayed for {Component} (Old={OldValue}, New={NewValue}, IsValid={IsValid})")]
    private static partial void LogValidationRelayed(ILogger logger, string component, float oldValue, float newValue, bool isValid);

    private void LogValidationRelayed(string component, float oldValue, float newValue, bool isValid)
    {
        if (this.logger is ILogger logger)
        {
            LogValidationRelayed(logger, component, oldValue, newValue, isValid);
        }
    }

    [LoggerMessage(EventId = 3701, Level = LogLevel.Trace, Message = "[VectorBox] SetValues called: X={X}, Y={Y}, Z={Z}, PreserveIndeterminate={Preserve}")]
    private static partial void LogSetValues(ILogger logger, float x, float y, float z, bool preserve);

    [Conditional("DEBUG")]
    private void LogSetValues(float x, float y, float z, bool preserve)
    {
        if (this.logger is ILogger logger)
        {
            LogSetValues(logger, x, y, z, preserve);
        }
    }

    [LoggerMessage(EventId = 3704, Level = LogLevel.Trace, Message = "[VectorBox] Component {Component} value change requested: Old={OldValue}, New={NewValue}, Valid={IsValid}")]
    private static partial void LogComponentValueChanged(ILogger logger, string component, float oldValue, float newValue, bool isValid);

    [Conditional("DEBUG")]
    private void LogComponentValueChanged(string component, float oldValue, float newValue, bool isValid)
    {
        if (this.logger is ILogger logger)
        {
            LogComponentValueChanged(logger, component, oldValue, newValue, isValid);
        }
    }

    [LoggerMessage(EventId = 3705, Level = LogLevel.Trace, Message = "[VectorBox] Vector updated to X={X}, Y={Y}, Z={Z}")]
    private static partial void LogVectorUpdated(ILogger logger, float x, float y, float z);

    [Conditional("DEBUG")]
    private void LogVectorUpdated(float x, float y, float z)
    {
        if (this.logger is ILogger logger)
        {
            LogVectorUpdated(logger, x, y, z);
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
