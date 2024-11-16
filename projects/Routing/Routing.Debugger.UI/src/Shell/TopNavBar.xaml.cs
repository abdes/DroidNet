// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Events;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.System;

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

    [ObservableProperty]
    private string? url;

    private IDisposable? routerEventsSub;

    public TopNavBar() => this.InitializeComponent();

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

    public void Dispose()
    {
        this.routerEventsSub?.Dispose();
        GC.SuppressFinalize(this);
    }

    private void UrlTextBox_KeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (e.Key == VirtualKey.Enter && this.Url != null)
        {
            this.Router.Navigate(this.Url, new FullNavigation());
        }
    }

    private void Reload(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        if (this.Url != null)
        {
            this.Router.Navigate(this.Url, new FullNavigation());
        }
    }
}
