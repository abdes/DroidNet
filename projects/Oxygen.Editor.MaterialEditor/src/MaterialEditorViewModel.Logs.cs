// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>Logging helpers for <see cref="MaterialEditorViewModel"/>.</summary>
public sealed partial class MaterialEditorViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Pending material work failed while disposing the editor.")]
    private static partial void LogPendingWorkFailedDuringDispose(ILogger logger, Exception exception);

    private void LogPendingWorkFailedDuringDispose(Exception exception)
        => LogPendingWorkFailedDuringDispose(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Could not refresh shared asset status for material {Material}.")]
    private static partial void LogAssetStatusFailed(ILogger logger, Exception exception, AssetPath material);

    private void LogAssetStatusFailed(Exception exception, AssetPath material)
        => LogAssetStatusFailed(this.logger, exception, material);
}
