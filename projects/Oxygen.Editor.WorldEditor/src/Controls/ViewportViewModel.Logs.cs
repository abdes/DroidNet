// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.WorldEditor.Controls;

/// <summary>
/// Logging helpers for <see cref="ViewportViewModel"/>.
/// </summary>
public partial class ViewportViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ViewportViewModel initialized. DocumentId: {DocumentId}")]
    private static partial void LogInitialized(ILogger logger, Guid documentId);

    [Conditional("DEBUG")]
    private void LogInitialized()
        => LogInitialized(this.logger, this.DocumentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Effective theme override set to {Theme}")]
    private static partial void LogEffectiveThemeSet(ILogger logger, ElementTheme theme);

    [Conditional("DEBUG")]
    private void LogEffectiveThemeSet(ElementTheme theme)
        => LogEffectiveThemeSet(this.logger, theme);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Error rebuilding menus after theme change")]
    private static partial void LogMenuRebuildFailed(ILogger logger, Exception exception);

    private void LogMenuRebuildFailed(Exception exception)
        => LogMenuRebuildFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ResolveIcon called with empty or null name")]
    private static partial void LogResolveIconEmptyName(ILogger logger);

    [Conditional("DEBUG")]
    private void LogResolveIconEmptyName()
        => LogResolveIconEmptyName(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ResolveIcon called before theme seeded")]
    private static partial void LogResolveIconBeforeTheme(ILogger logger);

    [Conditional("DEBUG")]
    private void LogResolveIconBeforeTheme()
        => LogResolveIconBeforeTheme(this.logger);
}
