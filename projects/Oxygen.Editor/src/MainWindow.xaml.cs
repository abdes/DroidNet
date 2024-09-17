// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Routing;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Helpers;

/// <summary>The application main window, which also acts as a <see cref="IOutletContainer" /> for some routes.</summary>
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    private readonly IViewLocator viewLocator;

    /*
     private readonly Microsoft.UI.Dispatching.DispatcherQueue dispatcherQueue;
     private readonly UISettings settings;
    */

    private object? shellViewModel;

    [ObservableProperty]
    private UIElement? shellView;

    public MainWindow(IViewLocator viewLocator)
    {
        this.InitializeComponent();

        this.viewLocator = viewLocator;

        this.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets/WindowIcon.ico"));
        this.Content = null;
        this.Title = "AppDisplayName".GetLocalized();

        /* TODO: refactor theme management
        // Theme change code picked from https://github.com/microsoft/WinUI-Gallery/pull/1239
        this.dispatcherQueue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread();
        this.settings = new UISettings();
        this.settings.ColorValuesChanged
            += this.Settings_ColorValuesChanged; // cannot use FrameworkElement.ActualThemeChanged event
        */
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

        // Set the ViewModel property of the view here, so that we don't lose
        // the view model instance we just created and which is the one that
        // must be associated with this view.
        //
        // This must be done here because the MainWindow does not have a
        // ViewModel and does not use the ViewModelToViewConverter.
        if (view is IViewFor hasViewModel)
        {
            hasViewModel.ViewModel = viewModel;
        }
        else
        {
            throw new InvalidViewTypeException($"invalid view type; not an {nameof(IViewFor)}")
            {
                ViewType = view.GetType(),
            };
        }

        if (!view.GetType().IsAssignableTo(typeof(UIElement)))
        {
            throw new InvalidViewTypeException($"invalid view type; not a {nameof(UIElement)}")
            {
                ViewType = view.GetType(),
            };
        }

        this.ShellView = (UIElement)view;
    }

    // this handles updating the caption button colors correctly when windows system theme is changed
    // while the app is open
    // This calls comes off-thread, hence we will need to dispatch it to current application's thread
    /* TODO: refactor theme management
    private void Settings_ColorValuesChanged(UISettings sender, object args)
        => this.dispatcherQueue.TryEnqueue(TitleBarHelper.ApplySystemThemeToCaptionButtons);
    */
}
