// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.WinPackagedApp;

using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Routing;
using DroidNet.Samples.Services;
using DroidNet.Samples.Settings;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.Graphics;

/// <summary>
/// Represents the main window of the user interface.
/// </summary>
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    private readonly IViewLocator viewLocator;
    private readonly IAppThemeModeService appThemeModeService;
    private readonly AppearanceSettingsService appearanceSettings;

    private object? shellViewModel;

    [ObservableProperty]
    private UIElement? shellView;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainWindow" /> class.
    /// </summary>
    /// <remarks>
    /// This window is created and activated when the <see cref="viewLocator" /> is launched. This
    /// approach ensures that window creation and destruction are managed by the application itself.
    /// This is crucial in applications where multiple windows exist, as it might not be clear which
    /// window is the main one, which impacts the UI lifetime. The window does not have a view model
    /// and does not need one. Windows are solely responsible for window-specific tasks, while the
    /// 'shell' view inside the window handles loading the appropriate content based on the active
    /// route or state of the application.
    /// </remarks>
    /// <param name="viewLocator">
    /// The view locator responsible for resolving the shell view model into its corresponding view,
    /// used as the window's content.
    /// </param>
    /// <param name="appThemeModeService">
    /// TODO: decide if we keep the window in charge of requesting to apply the theme to its content.
    /// </param>
    /// <param name="appearanceSettings">
    /// The settings service, which will provide settings to customize the window's appearance.
    /// </param>
    public MainWindow(
        IViewLocator viewLocator,
        IAppThemeModeService appThemeModeService,
        AppearanceSettingsService appearanceSettings)
    {
        this.InitializeComponent();

        this.appearanceSettings = appearanceSettings;
        this.viewLocator = viewLocator;
        this.appThemeModeService = appThemeModeService;

        //this.AppWindow.SetPresenter(AppWindowPresenterKind.Overlapped);
        const int width = 1200;
        const int height = 600;
        var workArea = DisplayArea.Primary.WorkArea;
        this.AppWindow.MoveAndResize(
            new RectInt32((workArea.Width - width) / 2, (workArea.Height - height) / 2, width, height),
            DisplayArea.Primary);

        this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode);
        appearanceSettings.PropertyChanged += this.AppearanceSettingsOnPropertyChanged;

        this.Closed += (_, _) => _ = this.appearanceSettings.SaveSettings();
    }

    /// <inheritdoc />
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (this.shellViewModel != viewModel)
        {
            if (this.shellViewModel is IDisposable resource)
            {
                resource.Dispose();
            }

            this.shellViewModel = viewModel;
        }

        var view = this.viewLocator.ResolveView(viewModel) ??
                   throw new MissingViewException { ViewModelType = viewModel.GetType() };

        // Set the ViewModel property of the view to ensure the correct view model instance is associated with this view.
        if (view is IViewFor hasViewModel)
        {
            hasViewModel.ViewModel = viewModel;
        }
        else
        {
            throw new InvalidViewTypeException($"Invalid view type; not an {nameof(IViewFor)}")
            {
                ViewType = view.GetType(),
            };
        }

        if (!view.GetType().IsAssignableTo(typeof(UIElement)))
        {
            throw new InvalidViewTypeException($"Invalid view type; not a {nameof(UIElement)}")
            {
                ViewType = view.GetType(),
            };
        }

        this.ShellView = (UIElement)view;
    }

    private void AppearanceSettingsOnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName?.Equals(nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal) == true)
        {
            Debug.WriteLine($"Applying theme `{this.appearanceSettings.AppThemeMode}` to {nameof(MainWindow)}");
            this.DispatcherQueue.TryEnqueue(
                () => this.appThemeModeService.ApplyThemeMode(this, this.appearanceSettings.AppThemeMode));
        }
    }
}
