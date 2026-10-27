// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor;

/// <summary>Logging for <see cref="ModelEditorViewModel"/>.</summary>
public sealed partial class ModelEditorViewModel
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Reimport of model {Source} requested from its Model Editor")]
    private static partial void LogReimportRequested(ILogger logger, string source);

    [LoggerMessage(Level = LogLevel.Error, Message = "Reimport of model {Source} failed")]
    private static partial void LogReimportFailed(ILogger logger, Exception exception, string source);

    [LoggerMessage(Level = LogLevel.Information, Message = "Placing model {Source} in the open scene from its Model Editor")]
    private static partial void LogAddToSceneRequested(ILogger logger, string source);

    private void LogReimportRequested(AssetPath source) => LogReimportRequested(this.logger, source.VirtualPath);

    private void LogReimportFailed(Exception exception, AssetPath source) => LogReimportFailed(this.logger, exception, source.VirtualPath);

    private void LogAddToSceneRequested(AssetPath source) => LogAddToSceneRequested(this.logger, source.VirtualPath);
}
