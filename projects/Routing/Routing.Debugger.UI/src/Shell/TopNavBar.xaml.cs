// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Windows.Controls;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Events;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.System;

namespace DroidNet.Routing.Debugger.UI.Shell;

/// <summary>
/// A custom control for the debugger top navigation bar.
/// </summary>
[ObservableObject]
public sealed partial class TopNavBar : IDisposable
{
    private static readonly DependencyProperty RouterProperty = DependencyProperty.Register(
        nameof(Router),
        typeof(IRouter),
        typeof(TopNavBar),
        new PropertyMetadata(default(IRouter)));

    private IDisposable? routerEventsSub;

    /// <summary>
    /// Initializes a new instance of the <see cref="TopNavBar"/> class.
    /// </summary>
    /// <remarks>
    /// This constructor sets up the initial state of the TopNavBar control and calls the
    /// <see cref="InitializeComponent"/> method to initialize the UI components defined in XAML.
    /// </remarks>
    public TopNavBar()
    {
        this.InitializeComponent();
    }

    [ObservableProperty]
    public partial string? Url { get; set; }

    /// <summary>
    /// Gets or sets the router instance used for navigation.
    /// </summary>
    /// <remarks>
    /// When the router is set, this property subscribes to the <see cref="IRouter.Events"/> stream
    /// to listen for <see cref="NavigationEnd"/> events. Upon receiving such an event, it updates
    /// the <see cref="Url"/> property with the URL from the event.
    /// </remarks>
    public IRouter Router
    {
        get => (IRouter)this.GetValue(RouterProperty);
        set
        {
            this.SetValue(RouterProperty, value);
            this.routerEventsSub = this.Router.Events
                .OfType<NavigationEnd>()
                .Subscribe(e => this.Url = e.Url);
        }
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.routerEventsSub?.Dispose();
        GC.SuppressFinalize(this);
    }

    private async void UrlTextBox_KeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (e.Key == VirtualKey.Enter && this.Url != null)
        {
            await this.Router.NavigateAsync(this.Url, new FullNavigation()).ConfigureAwait(true);
        }
    }

    private async void Reload(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.Url != null)
        {
            await this.Router.NavigateAsync(this.Url, new FullNavigation()).ConfigureAwait(true);
        }
    }
}
