// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

/* ReSharper disable PrivateFieldCanBeConvertedToLocalVariable */

namespace DroidNet.Controls.Demo;

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.WinUI;
using DroidNet.Mvvm;
using DroidNet.Routing;
using Microsoft.UI.Xaml;

/// <summary>The User Interface's main window.</summary>
/// <remarks>
/// <para>
/// This window is created and activated when the Application is Launched. This is preferred to the alternative of doing that in
/// the hosted service to keep the control of window creation and destruction under the application itself. Not all applications
/// have a single window, and it is often not obvious which window is considered the main window, which is important in
/// determining when the UI lifetime ends.
/// </para>
/// <para>
/// The window does not have a view model and does not need one. The design principle is that windows are here only to do window
/// stuff and the content inside the window is provided by a 'shell' view that will in turn load the appropriate content based on
/// the application active route or state.
/// </para>
/// </remarks>
[ExcludeFromCodeCoverage]
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    private readonly IViewLocator viewLocator;
    private object? contentViewModel;

    [ObservableProperty]
    private UIElement? content;

    public MainWindow(IViewLocator viewLocator)
    {
        this.InitializeComponent();

        this.viewLocator = viewLocator;

        this.AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets/WindowIcon.ico"));
        this.Content = null;
        this.Title = "AppDisplayName".GetLocalized();
    }

    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        if (this.contentViewModel != viewModel)
        {
            if (this.contentViewModel is IDisposable resource)
            {
                resource.Dispose();
            }

            this.contentViewModel = viewModel;
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

        this.Content = (UIElement)view;
    }
}
