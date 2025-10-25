// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using DroidNet.Aura.WindowManagement;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Decoration;

/// <summary>
/// Service responsible for applying window chrome decorations (title bar customization).
/// </summary>
/// <remarks>
/// <para>
/// This service subscribes to window lifecycle events and automatically applies chrome
/// settings when windows are created.
/// </para>
/// <para>
/// Chrome settings include:
/// <list type="bullet">
/// <item><description>ExtendsContentIntoTitleBar - Enables custom title bar</description></item>
/// <item><description>TitleBar height preferences</description></item>
/// </list>
/// </para>
/// </remarks>
public sealed partial class WindowChromeService
{
    private readonly IWindowManagerService windowManager;
    private readonly ILogger<WindowChromeService> logger;
    private readonly IDisposable windowCreatedSubscription;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowChromeService"/> class.
    /// </summary>
    /// <param name="windowManager">The window manager service to subscribe to window events.</param>
    /// <param name="logger">Logger for diagnostic messages.</param>
    public WindowChromeService(
        IWindowManagerService windowManager,
        ILogger<WindowChromeService> logger)
    {
        ArgumentNullException.ThrowIfNull(windowManager);
        ArgumentNullException.ThrowIfNull(logger);

        this.windowManager = windowManager;
        this.logger = logger;

        // Subscribe to window created events
        this.windowCreatedSubscription = this.windowManager.WindowEvents
            .Where(evt => evt.EventType == WindowLifecycleEventType.Created)
            .Subscribe(evt => this.ApplyChrome(evt.Context));

        this.LogServiceInitialized();
    }

    /// <summary>
    /// Applies chrome settings to a window based on its decoration options.
    /// </summary>
    /// <param name="context">The window context to apply chrome to.</param>
    public void ApplyChrome(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        var window = context.Window;
        var decoration = context.Decorations;

        // If no decoration is specified, use default chrome behavior (enabled)
        var chromeEnabled = decoration?.ChromeEnabled ?? true;

        this.LogApplyingChrome(context.Id, chromeEnabled);

        try
        {
            // Special handling for Tool windows that use custom chrome
            if (context.Category == WindowCategory.Tool && !chromeEnabled)
            {
                this.ApplyToolWindowChrome(window, context.Id);
                return;
            }

            this.ApplyStandardChrome(window, decoration, chromeEnabled, context.Id);
        }
#pragma warning disable CA1031 // Do not catch general exception types - intentional for graceful degradation
        catch (Exception ex)
#pragma warning restore CA1031
        {
            this.LogChromeApplicationFailed(ex, context.Id);
        }
    }

    /// <summary>
    /// Applies chrome to all open windows.
    /// </summary>
    public void ApplyChrome() => this.ApplyChrome(_ => true);

    /// <summary>
    /// Applies chrome to windows matching a predicate.
    /// </summary>
    /// <param name="predicate">Predicate to filter which windows should have chrome applied.</param>
    public void ApplyChrome(Func<WindowContext, bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);

        this.LogApplyingChromeToWindows();

        foreach (var context in this.windowManager.OpenWindows.Where(predicate))
        {
            this.ApplyChrome(context);
        }
    }

    private void ApplyToolWindowChrome(Window window, Guid windowId)
    {
        // Tool windows with custom chrome need to completely hide the system title bar
        // including caption buttons, while still having a border
        if (window.AppWindow.Presenter is OverlappedPresenter toolPresenter)
        {
            toolPresenter.SetBorderAndTitleBar(hasBorder: true, hasTitleBar: false);
            this.LogToolWindowChromeApplied(windowId);
        }

        // Extend content into title bar area to use custom title bar
        window.ExtendsContentIntoTitleBar = true;
        this.LogChromeApplied(windowId);
    }

    private void ApplyStandardChrome(Window window, WindowDecorationOptions? decoration, bool chromeEnabled, Guid windowId)
    {
        // Set ExtendsContentIntoTitleBar based on chrome settings
        // - ChromeEnabled=true: Use Aura chrome (extend content into title bar)
        // - ChromeEnabled=false: Use system chrome (don't extend content)
        window.ExtendsContentIntoTitleBar = chromeEnabled;

        if (chromeEnabled)
        {
            // Apply title bar height if specified
            var titleBarHeight = decoration?.TitleBar?.Height;
            if (titleBarHeight.HasValue)
            {
                // WinUI doesn't support arbitrary height, only predefined options
                // Map height to closest standard option
                window.AppWindow.TitleBar.PreferredHeightOption = titleBarHeight.Value switch
                {
                    >= 40.0 => TitleBarHeightOption.Tall,
                    _ => TitleBarHeightOption.Standard,
                };
            }
            else
            {
                window.AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Standard;
            }
        }

        // Apply window button visibility settings
        var buttons = decoration?.Buttons;
        if (buttons is not null && window.AppWindow.Presenter is OverlappedPresenter presenter)
        {
            this.LogApplyingButtons(windowId, buttons.ShowMinimize, buttons.ShowMaximize);

            // Configure which buttons are enabled
            presenter.IsMinimizable = buttons.ShowMinimize;
            presenter.IsMaximizable = buttons.ShowMaximize;
        }

        this.LogChromeApplied(windowId);
    }

    [LoggerMessage(
        EventId = 7000,
        Level = LogLevel.Information,
        Message = "[WindowChromeService] Service initialized and subscribed to window events")]
    private partial void LogServiceInitialized();

    [LoggerMessage(
        EventId = 7001,
        Level = LogLevel.Debug,
        Message = "[WindowChromeService] Applying chrome to window {WindowId} (ChromeEnabled: {ChromeEnabled})")]
    private partial void LogApplyingChrome(Guid windowId, bool chromeEnabled);

    [LoggerMessage(
        EventId = 7002,
        Level = LogLevel.Information,
        Message = "[WindowChromeService] Chrome applied successfully to window {WindowId}")]
    private partial void LogChromeApplied(Guid windowId);

    [LoggerMessage(
        EventId = 7003,
        Level = LogLevel.Error,
        Message = "[WindowChromeService] Failed to apply chrome to window {WindowId}")]
    private partial void LogChromeApplicationFailed(Exception ex, Guid windowId);

    [LoggerMessage(
        EventId = 7004,
        Level = LogLevel.Debug,
        Message = "[WindowChromeService] Applying chrome to all matching windows")]
    private partial void LogApplyingChromeToWindows();

    [LoggerMessage(
        EventId = 7005,
        Level = LogLevel.Debug,
        Message = "[WindowChromeService] Applying button visibility to window {WindowId} (Minimize: {ShowMinimize}, Maximize: {ShowMaximize})")]
    private partial void LogApplyingButtons(Guid windowId, bool showMinimize, bool showMaximize);

    [LoggerMessage(
        EventId = 7006,
        Level = LogLevel.Debug,
        Message = "[WindowChromeService] Applied tool window chrome - removed system title bar for window {WindowId}")]
    private partial void LogToolWindowChromeApplied(Guid windowId);
}
