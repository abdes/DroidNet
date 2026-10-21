// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Materials.Documents;

/// <summary>Logging helpers for <see cref="MaterialValidation"/>.</summary>
public sealed partial class MaterialValidation
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Loaded the material schemas for editor validation.")]
    private static partial void LogSchemaLoaded(ILogger logger);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogSchemaLoaded()
        => LogSchemaLoaded(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The material schemas are unavailable; saves are still validated by the material writer.")]
    private static partial void LogSchemaUnavailable(ILogger logger, Exception exception);

    private void LogSchemaUnavailable(Exception exception)
        => LogSchemaUnavailable(this.logger, exception);
}
