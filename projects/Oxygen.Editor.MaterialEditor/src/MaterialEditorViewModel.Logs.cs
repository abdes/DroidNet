// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.MaterialEditor;

#pragma warning disable SA1204 // Each log method keeps its static and instance forms together

/// <summary>Log messages.</summary>
public sealed partial class MaterialEditorViewModel
{
    [LoggerMessage(SkipEnabledCheck = true, Level = LogLevel.Debug, Message = "Material editor shows the relocated material as '{Material}'.")]
    private static partial void LogFollowedRelocation(ILogger logger, Uri material);

    [System.Diagnostics.Conditional("DEBUG")]
    private void LogFollowedRelocation(Uri material)
        => LogFollowedRelocation(this.logger, material);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Pending material work failed while disposing the editor.")]
    private partial void LogPendingWorkFailedDuringDispose(Exception exception);
}
