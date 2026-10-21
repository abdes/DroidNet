// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>Logging helpers for <see cref="NewProjectDialog"/>.</summary>
public sealed partial class NewProjectDialog
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Could not open project location picker.")]
    private static partial void LogFailedToOpenPicker(ILogger logger, Exception exception);
}
