// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Content;

/// <summary>
///     Logging helpers for <see cref="SceneContentDemand"/>.
/// </summary>
internal sealed partial class SceneContentDemand
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Unable to prepare preview content for {Asset}.")]
    private static partial void LogDemandFailure(ILogger logger, Exception exception, AssetPath asset);

    private void LogDemandFailure(Exception exception, AssetPath asset)
        => LogDemandFailure(this.logger, exception, asset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Demanding a preview cook of {Asset}.")]
    private static partial void LogDemandStarted(ILogger logger, AssetPath asset);

    [Conditional("DEBUG")]
    private void LogDemandStarted(AssetPath asset)
        => LogDemandStarted(this.logger, asset);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "The scene no longer references {Asset}; cancelling its preview cook.")]
    private static partial void LogDemandCancelled(ILogger logger, AssetPath asset);

    [Conditional("DEBUG")]
    private void LogDemandCancelled(AssetPath asset)
        => LogDemandCancelled(this.logger, asset);
}
