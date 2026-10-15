// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.Play;

/// <summary>Logger message definitions for <see cref="PlaySessionSettingsService"/>.</summary>
public sealed partial class PlaySessionSettingsService
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "The play session preferences could not be read; the defaults apply.")]
    private partial void LogPreferencesUnreadable(Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Discarded play session preferences of version {Version}; the defaults apply.")]
    private partial void LogPreferencesDiscarded(int version);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The play session preferences could not be saved.")]
    private partial void LogPreferencesNotSaved(Exception exception);
}
