// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Logging methods for the <see cref="IWindowContextFactory"/> class.
/// </summary>
public partial class WindowContextFactory
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "New window context for window: Id='{windowId}', Title='{Title}' (decoration={HasDecoration}, metdata={HasMetadata})")]
    private static partial void LogCreate(ILogger logger, ulong windowId, string title, bool hasDecoration, bool hasMetadata);

    [Conditional("DEBUG")]
    private void LogCreate(WindowContext context)
        => LogCreate(
            this.logger,
            context.Id.Value,
            context.Window.Title,
            context.Decorations is not null,
            context.Metadata is not null);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Menu provider '{ProviderId}' not found; window '{windowId}' will not have a menu")]
    private static partial void LogMenuProviderNotFound(ILogger logger, string providerId, ulong windowId);

    private void LogMenuProviderNotFound(string providerId, WindowContext context)
        => LogMenuProviderNotFound(this.logger, providerId, context.Id.Value);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Successfully created menu source with provider '{ProviderId}', for window '{windowId}'")]
    private static partial void LogMenuSourceCreated(ILogger logger, string providerId, ulong windowId);

    [Conditional("DEBUG")]
    private void LogMenuSourceCreated(string providerId, WindowContext context)
        => LogMenuSourceCreated(this.logger, providerId, context.Id.Value);
}
