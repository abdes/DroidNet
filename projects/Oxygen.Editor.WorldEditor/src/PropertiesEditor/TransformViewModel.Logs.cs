// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

public partial class TransformViewModel
{
    [LoggerMessage(EventId = 3700, Level = LogLevel.Trace, Message = "[TransformViewModel] Constructed (HasLoggerFactory={HasFactory})")]
    private static partial void LogConstructed(ILogger logger, bool hasFactory);

    private void LogConstructed()
    {
        if (this.logger is ILogger lg)
        {
            LogConstructed(lg, this.LoggerFactory is not null);
        }
    }

    [LoggerMessage(EventId = 3701, Level = LogLevel.Trace, Message = "[TransformViewModel] UpdateValues called (Count={Count})")]
    private static partial void LogUpdateValues(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogUpdateValues(int count)
    {
        if (this.logger is ILogger lg)
        {
            LogUpdateValues(lg, count);
        }
    }

    [LoggerMessage(EventId = 3702, Level = LogLevel.Trace, Message = "[TransformViewModel] Applying property {Property} value {Value} to {TargetCount} items")]
    private static partial void LogApplyingChange(ILogger logger, string property, float value, int targetCount);

    [Conditional("DEBUG")]
    private void LogApplyingChange(string property, float value, int targetCount)
    {
        if (this.logger is ILogger lg)
        {
            LogApplyingChange(lg, property, value, targetCount);
        }
    }

    [LoggerMessage(EventId = 3703, Level = LogLevel.Error, Message = "[TransformViewModel] Apply failed for {Property}")]
    private static partial void LogApplyFailed(ILogger logger, string property, Exception exception);

    private void LogApplyFailed(string property, Exception ex)
    {
        if (this.logger is ILogger lg)
        {
            LogApplyFailed(lg, property, ex);
        }
    }
}
