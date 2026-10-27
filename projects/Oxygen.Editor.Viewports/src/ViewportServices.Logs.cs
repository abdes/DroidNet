// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Documents;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.Viewports;

/// <summary>Logging for <see cref="ViewportServices"/>.</summary>
public sealed partial class ViewportServices
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected the selection outline of document {DocumentId}; no engine session is running")]
    private static partial void LogSelectionOutlineRejected(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogSelectionOutlineRejected(DocumentId documentId)
        => LogSelectionOutlineRejected(this.logger, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The runtime rejected hiding the transform gizmo for document {DocumentId}; no engine session is running")]
    private static partial void LogTransformGizmoRejected(ILogger logger, DocumentId documentId);

    [Conditional("DEBUG")]
    private void LogTransformGizmoRejected(DocumentId documentId)
        => LogTransformGizmoRejected(this.logger, documentId);

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "The transform gizmo could not be hidden for document {DocumentId}")]
    private static partial void LogTransformGizmoFailed(ILogger logger, DocumentId documentId, Exception ex);

    private void LogTransformGizmoFailed(DocumentId documentId, Exception ex)
        => LogTransformGizmoFailed(this.logger, documentId, ex);
}
