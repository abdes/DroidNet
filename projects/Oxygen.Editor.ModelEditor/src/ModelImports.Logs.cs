// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ModelEditor;

/// <summary>Logging for <see cref="ModelImports"/>.</summary>
public sealed partial class ModelImports
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Reimport of model {Source} requested from its Model Editor")]
    private static partial void LogReimportRequested(ILogger logger, string source);

    [LoggerMessage(Level = LogLevel.Information, Message = "Save of the import settings of model {Source} requested: the record is replaced and the model reimported")]
    private static partial void LogSaveRequested(ILogger logger, string source);

    [LoggerMessage(Level = LogLevel.Error, Message = "Import of model {Source} failed")]
    private static partial void LogImportFailed(ILogger logger, Exception exception, string source);

    private void LogReimportRequested(AssetPath source) => LogReimportRequested(this.logger, source.VirtualPath);

    private void LogSaveRequested(AssetPath source) => LogSaveRequested(this.logger, source.VirtualPath);

    private void LogImportFailed(Exception exception, AssetPath source) => LogImportFailed(this.logger, exception, source.VirtualPath);
}
