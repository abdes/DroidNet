// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Logging methods for <see cref="ExposureSectionViewModel"/>.</summary>
public sealed partial class ExposureSectionViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The shared asset catalog could not be refreshed for the metering mask picker.")]
    private static partial void LogMeteringMaskRefreshFailed(ILogger logger, Exception exception);

    private void LogMeteringMaskRefreshFailed(Exception exception) => LogMeteringMaskRefreshFailed(this.logger, exception);
}
