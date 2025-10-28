// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura;

/// <summary>
///     Represents the view model for the main shell of the application, providing decorations and
///     enhancements to the window content.
/// </summary>
public partial class MainShellViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "({Packaged}) Asset file not found at: {AssetPath}")]
    private static partial void LogAssetFileNotFound(ILogger logger, string packaged, string assetPath, Exception? ex);

    [Conditional("DEBUG")]
    private void LogAssetFileNotFound(bool isPackaged, string assetPath, Exception? ex = null)
        => LogAssetFileNotFound(this.logger, isPackaged ? "Packaged" : "Unpackaged", assetPath, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "({Packaged}) Icon '{RelativePath}' will use asset at: {Uri}")]
    private static partial void LogIconFound(ILogger logger, string packaged, string relativePath, Uri uri);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "({Packaged}) Could not find any usable asset for icon with relative path '{RelativePath}'")]
    private static partial void LogIconNotFound(ILogger logger, string packaged, string relativePath);

    private void LogIconAsset(bool isPackaged, string relativePath, Uri? uri)
    {
        if (uri is not null)
        {
            LogIconFound(this.logger, isPackaged ? "Packaged" : "Unpackaged", relativePath, uri);
        }
        else
        {
            LogIconNotFound(this.logger, isPackaged ? "Packaged" : "Unpackaged", relativePath);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Custom title bar setup for window '{Window} with height '{TitleBarHeight}")]
    private static partial void LogWindowTitleBarSetup(ILogger logger, string window, TitleBarHeightOption titleBarHeight);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Not setting up custom title bar for window '{Window}', because it's not using Aura chrome")]
    private static partial void LogNoWindowTitleBarSetup(ILogger logger, string window);

    [Conditional("DEBUG")]
    private void LogWindowTitleBarSetup(bool chromeEnabled)
    {
        Debug.Assert(this.Window is not null, "Window should not be null when logging title bar setup.");
        var windowTitle = this.Window.Title;

        if (!chromeEnabled)
        {
            LogNoWindowTitleBarSetup(this.logger, windowTitle);
            return;
        }

        var titleBarHeight = this.Window.AppWindow.TitleBar.PreferredHeightOption;
        LogWindowTitleBarSetup(this.logger, windowTitle, titleBarHeight);
    }
}
