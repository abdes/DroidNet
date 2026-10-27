// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor;

/// <summary>Logging for <see cref="ModelEditorViewModel"/>.</summary>
public sealed partial class ModelEditorViewModel
{
    [LoggerMessage(Level = LogLevel.Warning, Message = "The geometry {Geometry} of the selected mesh could not be read; its import defaults are not shown")]
    private static partial void LogSelectedMeshUnreadable(ILogger logger, Exception exception, string geometry);

    [LoggerMessage(Level = LogLevel.Information, Message = "Placing model {Source} in the open scene from its Model Editor")]
    private static partial void LogAddToSceneRequested(ILogger logger, string source);

    private void LogSelectedMeshUnreadable(Exception exception, AssetPath geometry) => LogSelectedMeshUnreadable(this.logger, exception, geometry.VirtualPath);

    private void LogAddToSceneRequested(AssetPath source) => LogAddToSceneRequested(this.logger, source.VirtualPath);
}
