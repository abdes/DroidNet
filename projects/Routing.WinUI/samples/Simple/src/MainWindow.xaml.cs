// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using Microsoft.UI.Xaml;

/// <summary>The application main window, which also acts as a <see cref="IOutletContainer" /> for some routes.</summary>
[ObservableObject]
public sealed partial class MainWindow : IOutletContainer
{
    private readonly IViewLocator viewLocator;
    private object? shellViewModel;

    [ObservableProperty]
    private UIElement? shellView;

    /// <summary>Initializes a new instance of the <see cref="MainWindow" /> class.</summary>
    /// <remarks>
    /// <para>
    /// This window is created and activated when the <see cref="viewLocator" /> is Launched. This is preferred to the alternative
    /// of doing that in the hosted service to keep the control of window creation and destruction under the application itself.
    /// Not all applications have a single window, and it is often not obvious which window is considered the main window, which
    /// is important in determining when the UI lifetime ends.
    /// </para>
    /// <para>
    /// The window does not have a view model and does not need one. The design principle is that windows are here only to do
    /// window stuff and the content inside the window is provided by a 'shell' view that will in turn load the appropriate
    /// content based on the application active route or state.
    /// </para>
    /// </remarks>
    /// <param name="viewLocator">
    /// The view locator to resolve the shell view model into its corresponding view, so it can be used as the window's content.
    /// </param>
    public MainWindow(IViewLocator viewLocator)
    {
        this.InitializeComponent();

        this.viewLocator = viewLocator;
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
}
