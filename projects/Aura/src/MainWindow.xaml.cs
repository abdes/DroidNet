// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura;

/// <summary>
/// Represents the main window of the user interface.
/// </summary>
public sealed partial class MainWindow : IOutletContainer, INotifyPropertyChanged
{
    private readonly IAppThemeModeService appThemeModeService;
    private readonly AppearanceSettingsService appearanceSettings;
    private readonly IDisposable autoSaveSubscription;
    private object? contentViewModel;

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

        // Setup auto-save with Rx debouncing
        // Convert PropertyChanged events to observable and debounce for 5 seconds
        this.autoSaveSubscription = Observable
            .FromEventPattern<PropertyChangedEventHandler, PropertyChangedEventArgs>(
                handler => this.appearanceSettings.PropertyChanged += handler,
                handler => this.appearanceSettings.PropertyChanged -= handler)
            .Throttle(TimeSpan.FromSeconds(5))
            .Subscribe(_ => this.SaveSettingsIfDirty());

        appearanceSettings.PropertyChanged += this.AppearanceSettingsOnPropertyChanged;

        this.Closed += this.OnWindowClosed;
    }

    /// <summary>
    /// Event raised when a property value changes.
    /// </summary>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets or sets the content view model.
    /// </summary>
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

    /// <summary>
    /// Saves the appearance settings if they have been modified.
    /// </summary>
    private void SaveSettingsIfDirty()
    {
        if (this.appearanceSettings.IsDirty)
        {
            var saved = this.appearanceSettings.SaveSettings();
            Debug.WriteLine($"Auto-save settings: {(saved ? "succeeded" : "failed")}");
        }
    }

    /// <summary>
    /// Handles the window closed event to ensure settings are saved before the window closes.
    /// </summary>
    private void OnWindowClosed(object sender, object args)
    {
        _ = sender;
        _ = args;

        // Dispose the auto-save subscription
        this.autoSaveSubscription.Dispose();

        // Save any pending changes
        this.SaveSettingsIfDirty();
    }

    /// <summary>
    /// Raises the PropertyChanged event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    private void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
