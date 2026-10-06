// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ProjectBrowser.Views;

public sealed partial class NewProjectDialog
{
    [LoggerMessage(Level = LogLevel.Error, Message = "Could not open project location picker.")]
    private static partial void LogFailedToOpenPicker(ILogger logger, Exception exception);
}
