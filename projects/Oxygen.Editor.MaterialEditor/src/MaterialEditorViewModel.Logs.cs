// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Log messages.</summary>
public sealed partial class MaterialEditorViewModel
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "Pending material work failed while disposing the editor.")]
    private partial void LogPendingWorkFailedDuringDispose(Exception exception);
}
