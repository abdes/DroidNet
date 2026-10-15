// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Play;

/// <summary>Logger message definitions for <see cref="PlayerSettingsSync"/>.</summary>
internal sealed partial class PlayerSettingsSync
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "The player did not apply the {Setting}: {Message}")]
    private partial void LogSettingRefused(string setting, string message);
}
