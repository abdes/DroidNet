// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Runtime.InteropServices;
using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using WinRT.Interop;

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Service responsible for applying window chrome decorations (title bar customization).
/// </summary>
/// <remarks>
///     This service subscribes to window lifecycle events and automatically applies chrome
///     settings when windows are created.
///     <para>
///     Chrome settings include:
///     <list type="bullet">
///     <item><description>ExtendsContentIntoTitleBar - Enables custom title bar</description></item>
///     <item><description>TitleBar height preferences</description></item>
///     </list>
///     </para>
/// </remarks>
public sealed partial class WindowChromeService
{
    private readonly IWindowManagerService windowManager;
    private readonly ILogger<WindowChromeService> logger;
    private readonly IDisposable windowCreatedSubscription;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowChromeService"/> class.
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
    ///     Applies chrome settings to a window based on its decoration options.
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
    ///     Applies chrome to all open windows.
    /// </summary>
    public void ApplyChrome() => this.ApplyChrome(_ => true);

    /// <summary>
    ///     Applies chrome to windows matching a predicate.
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

    [LibraryImport("dwmapi.dll")]
    [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
    private static partial int DwmSetWindowAttribute(nint hwnd, int attribute, ref int pvAttribute, int cbAttribute);

    private void ApplyStandardChrome(Window window, WindowDecorationOptions? decoration, bool chromeEnabled, WindowId windowId)
    {
        // Set ExtendsContentIntoTitleBar based on chrome settings
        // - ChromeEnabled=true: Use Aura chrome (extend content into title bar)
        // - ChromeEnabled=false: Use system chrome (don't extend content)
        window.ExtendsContentIntoTitleBar = chromeEnabled;

        var presenter = window.AppWindow.Presenter as OverlappedPresenter;

        // Apply button visibility, always
        // Apply window button visibility settings
        var buttons = decoration?.Buttons;
        if (buttons is not null)
        {
            this.LogApplyingButtons(windowId, buttons.ShowMinimize, buttons.ShowMaximize);

            // Configure which buttons are enabled
            _ = presenter?.IsMinimizable = buttons.ShowMinimize;
            _ = presenter?.IsMaximizable = buttons.ShowMaximize;
        }

        if (!chromeEnabled)
        {
            return;
        }

        // Setup border and title bar presence
        var hasBorder = decoration?.WithBorder == true;
        var hasTitleBar = decoration?.TitleBar is not null;

        presenter?.SetBorderAndTitleBar(hasBorder: hasBorder, hasTitleBar: hasTitleBar);

        var isResizable = decoration?.IsResizable ?? false;
        _ = presenter?.IsResizable = false;

        if (decoration?.TitleBar is { Height: { } titleBarHeight })
        {
            // WinUI doesn't support arbitrary height, only predefined options
            // Map height to closest standard option
            window.AppWindow.TitleBar.PreferredHeightOption = titleBarHeight switch
            {
                >= 40.0 => TitleBarHeightOption.Tall,
                _ => TitleBarHeightOption.Standard,
            };
        }

        // Do this last after the rest of the window is setup
        if (decoration?.RoundedCorners == false)
        {
            // Disable rounded corners
            const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
            var dwmwcpDoNotRound = 1;
            var hwnd = WindowNative.GetWindowHandle(window);
            _ = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, ref dwmwcpDoNotRound, sizeof(int));
        }

        this.LogChromeApplied(windowId);
    }
}
