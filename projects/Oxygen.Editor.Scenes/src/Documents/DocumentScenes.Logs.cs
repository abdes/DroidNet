// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Scenes.Documents;

/// <summary>Logging for <see cref="DocumentScenes"/>.</summary>
public sealed partial class DocumentScenes
{
    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "The kept preview of model '{Model}' cannot be shown; the model shows the default preview.")]
    private static partial void LogKeptPreviewRejected(ILogger logger, string model, Exception ex);

    private void LogKeptPreviewRejected(string model, Exception ex) => LogKeptPreviewRejected(this.logger, model, ex);
}
