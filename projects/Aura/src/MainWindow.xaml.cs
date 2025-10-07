// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura;

/// <summary>
/// Represents the main window of the user interface.
/// </summary>
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    private readonly IAppThemeModeService appThemeModeService;
    private readonly AppearanceSettingsService appearanceSettings;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainWindow" /> class.
    /// </summary>
    /// <remarks>
    /// This window is created and activated when the application is launched. This approach ensures
    /// that window creation and destruction are managed by the application itself. This is crucial
    /// in applications where multiple windows exist, as it might not be clear which window is the
    /// main one, which impacts the UI lifetime. The window does not have a view model and does not
    /// need one. Windows are solely responsible for window-specific tasks, while the 'shell' view
    /// inside the window handles loading the appropriate content based on the active route or state
    /// of the application.
    /// </remarks>
    /// <param name="appThemeModeService">
    /// TODO: decide if we keep the window in charge of requesting to apply the theme to its content.
    /// </param>
    /// <param name="appearanceSettings">
    /// The settings service, which will provide settings to customize the window's appearance.
    /// </param>
    public MainWindow(IAppThemeModeService appThemeModeService, AppearanceSettingsService appearanceSettings)
    {
        this.InitializeComponent();

        this.appearanceSettings = appearanceSettings;
        this.appThemeModeService = appThemeModeService;
        var workArea = DisplayArea.Primary.WorkArea;

        /*
        this.AppWindow.MoveAndResize(
            new RectInt32((workArea.Width - width) / 2, (workArea.Height - height) / 2, width, height),
            DisplayArea.Primary);
        */

        this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode);
        appearanceSettings.PropertyChanged += this.AppearanceSettingsOnPropertyChanged;

        this.Closed += (_, _) => _ = this.appearanceSettings.SaveSettings();
    }

    [ObservableProperty]
    public partial object? ContentViewModel { get; set; }

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (this.ContentViewModel != viewModel)
        {
            if (this.ContentViewModel is IDisposable resource)
            {
                resource.Dispose();
            }

            this.ContentViewModel = viewModel;
        }
    }

    private void AppearanceSettingsOnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal) == true)
        {
            Debug.WriteLine($"Applying theme `{this.appearanceSettings.AppThemeMode}` to {nameof(MainWindow)}");
            _ = this.DispatcherQueue.TryEnqueue(
                () => this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode));
        }
    }
}
