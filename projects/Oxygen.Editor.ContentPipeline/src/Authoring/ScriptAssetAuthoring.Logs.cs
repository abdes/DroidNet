// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.ContentPipeline.Authoring;

/// <inheritdoc cref="ScriptAssetAuthoring"/>
/// <remarks>Levels: Information for each file created; Warning for a file not created and why.</remarks>
public sealed partial class ScriptAssetAuthoring
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Created {Kind} '{Path}' from its template.")]
    private partial void LogCreated(ScriptAssetKind kind, string path);

    [LoggerMessage(Level = LogLevel.Warning, Message = "'{Name}' was not created in {Folder}: {Reason}")]
    private partial void LogNotCreated(string name, string folder, string reason);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Could not write the new file '{Path}'.")]
    private partial void LogWriteFailed(Exception exception, string path);
}
