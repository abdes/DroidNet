// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <inheritdoc cref="AutomaticCookService"/>
public sealed partial class AutomaticCookService
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Automatic cooking failed for changed source {Source}.")]
    private partial void LogSchedulingFailure(Exception exception, string source);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook on change is off; {Count} changed sources were not cooked.")]
    private partial void LogNotCooked(int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "{Count} changed sources were not cooked again: their published output already matches the files.")]
    private partial void LogAlreadyCooked(int count);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The cook status of {Count} changed sources could not be read; they are cooked.")]
    private partial void LogStatusUnavailable(Exception exception, int count);

    [LoggerMessage(Level = LogLevel.Information, Message = "Cook on change set to {Enabled} for the active project.")]
    private partial void LogCookOnChangeSet(bool enabled);
}
