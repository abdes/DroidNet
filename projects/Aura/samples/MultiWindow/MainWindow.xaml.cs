// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Aura;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// Represents the main window of the multi-window sample application.
/// </summary>
public sealed partial class MainWindow : IOutletContainer, INotifyPropertyChanged
{
    private readonly IAppThemeModeService appThemeModeService;
    private readonly AppearanceSettingsService appearanceSettings;
    private object? contentViewModel;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainWindow"/> class.
    /// </summary>
    /// <param name="appThemeModeService">The theme mode service.</param>
    /// <param name="appearanceSettings">The appearance settings service.</param>
    public MainWindow(IAppThemeModeService appThemeModeService, AppearanceSettingsService appearanceSettings)
    {
        this.InitializeComponent();

        this.appearanceSettings = appearanceSettings;
        this.appThemeModeService = appThemeModeService;

        // Apply theme to this window
        this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode);

        // Listen for theme changes
        this.appearanceSettings.PropertyChanged += this.AppearanceSettingsOnPropertyChanged;

        // Set default size
        this.AppWindow.Resize(new Windows.Graphics.SizeInt32(1024, 768));
    }

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc/>
    public object? ContentViewModel
    {
        get => this.contentViewModel;
        set
        {
            if (!Equals(this.contentViewModel, value))
            {
                this.contentViewModel = value;
                this.OnPropertyChanged(nameof(this.ContentViewModel));
            }
        }
    }

    /// <inheritdoc/>
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
            _ = this.DispatcherQueue.TryEnqueue(
                () => this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode));
        }
    }

    private void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
