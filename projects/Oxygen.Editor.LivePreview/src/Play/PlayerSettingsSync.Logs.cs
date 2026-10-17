// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
///     Logging helpers for <see cref="PlayerSettingsSync"/>.
/// </summary>
internal sealed partial class PlayerSettingsSync
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The player did not apply the {Setting}: {Message}")]
    private static partial void LogSettingRefused(ILogger logger, string setting, string message);

    private void LogSettingRefused(string setting, string message)
        => LogSettingRefused(this.logger, setting, message);
}
