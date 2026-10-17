// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.LivePreview.Content;

/// <summary>
///     Logging helpers for <see cref="SceneContentDemand"/>.
/// </summary>
internal sealed partial class SceneContentDemand
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unable to prepare preview content for {AssetUri}.")]
    private static partial void LogDemandFailure(ILogger logger, Exception exception, Uri assetUri);

    private void LogDemandFailure(Exception exception, Uri assetUri)
        => LogDemandFailure(this.logger, exception, assetUri);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Demanding a preview cook of {AssetUri}.")]
    private static partial void LogDemandStarted(ILogger logger, Uri assetUri);

    [Conditional("DEBUG")]
    private void LogDemandStarted(Uri assetUri)
        => LogDemandStarted(this.logger, assetUri);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The scene no longer references {AssetUri}; cancelling its preview cook.")]
    private static partial void LogDemandCancelled(ILogger logger, Uri assetUri);

    [Conditional("DEBUG")]
    private void LogDemandCancelled(Uri assetUri)
        => LogDemandCancelled(this.logger, assetUri);
}
