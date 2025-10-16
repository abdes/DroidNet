// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Reactive.Linq;
using DroidNet.Aura;
using DroidNet.Aura.WindowManagement;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// A tool window for lightweight utilities.
/// </summary>
public sealed partial class ToolWindow : Window
{
    private readonly IDisposable themeSubscription;

    /// <summary>
    /// Initializes a new instance of the <see cref="ToolWindow"/> class.
    /// </summary>
    /// <param name="windowManager">The window manager responsible for theme propagation.</param>
    /// <param name="themeModeService">The theme application service.</param>
    /// <param name="appearanceSettings">The appearance settings source.</param>
    public ToolWindow(
        IWindowManagerService windowManager,
        IAppThemeModeService themeModeService,
        AppearanceSettingsService appearanceSettings)
    {
        this.InitializeComponent();
        this.Title = "Tool Window";

        // Set a smaller default size for tool windows
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(400, 300));

        // Ensure the current theme is applied immediately and whenever the manager republishes lifecycle events.
        themeModeService.ApplyThemeMode(this, appearanceSettings.AppThemeMode);

        this.themeSubscription = windowManager.WindowEvents
            .Where(evt => ReferenceEquals(evt.Context.Window, this))
            .Where(evt => evt.EventType is WindowLifecycleEventType.Created or WindowLifecycleEventType.Activated)
            .Subscribe(_ => themeModeService.ApplyThemeMode(this, appearanceSettings.AppThemeMode));

        this.Closed += this.OnClosed;
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        this.themeSubscription.Dispose();
        this.Closed -= this.OnClosed;
    }
}
