// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// Logging methods for <see cref="CodeEditorService"/>.
/// </summary>
public sealed partial class CodeEditorService
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Discarded a code editor preference with unknown version {Version}")]
    private partial void LogPreferenceDiscarded(int version);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not read the code editor preference")]
    private partial void LogPreferenceUnreadable(Exception failure);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not save the code editor preference")]
    private partial void LogPreferenceNotSaved(Exception failure);

    [LoggerMessage(Level = LogLevel.Information, Message = "Opened `{Path}` in the code editor: {Program}")]
    private partial void LogEditorStarted(string path, string program);

    private void LogEditorStarted(string path, ProcessStartInfo start)
        => this.LogEditorStarted(path, start.FileName);
}
