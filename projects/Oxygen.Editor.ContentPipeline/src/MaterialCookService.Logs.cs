// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Logging helpers for <see cref="MaterialCookService"/>.</summary>
public sealed partial class MaterialCookService
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cooked material state could not be inspected for {MaterialUri}.")]
    private static partial void LogStateInspectionFailed(ILogger logger, AssetPath materialUri, Exception exception);

    private void LogStateInspectionFailed(AssetPath materialUri, Exception exception)
        => LogStateInspectionFailed(this.logger, materialUri, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Material cook rejected for {MaterialUri}: the source identity does not match the active project.")]
    private static partial void LogRejected(ILogger logger, AssetPath materialUri);

    private void LogRejected(AssetPath materialUri) => LogRejected(this.logger, materialUri);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Material cook {OperationId} completed for {MaterialUri}: {State}.")]
    private static partial void LogCompleted(ILogger logger, AssetPath materialUri, Guid operationId, MaterialCookState state);

    private void LogCompleted(AssetPath materialUri, Guid operationId, MaterialCookState state)
        => LogCompleted(this.logger, materialUri, operationId, state);
}
