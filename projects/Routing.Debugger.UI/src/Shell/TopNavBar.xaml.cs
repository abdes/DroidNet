// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.DependencyInjection;
using DroidNet.Routing.Events;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

/// <summary>
/// A custom control for the debugger top navigation bar.
/// </summary>
[ObservableObject]
public sealed partial class TopNavBar : IDisposable
{
    // TODO(abdes): avoid using Ioc.Default, set router in control
    private static readonly DependencyProperty RouterProperty = DependencyProperty.Register(
        nameof(Router),
        typeof(IRouter),
        typeof(TopNavBar),
        new PropertyMetadata(Ioc.Default.GetRequiredService<IRouter>()));

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

    public void Dispose() => this.routerEventsSub?.Dispose();

    private void UrlTextBox_KeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (e.Key == VirtualKey.Enter)
        {
            if (sender is TextBox control)
            {
                var text = control.Text;
            }

            if (this.Url != null)
            {
                this.Router.Navigate(this.Url);
            }
        }
    }
}
