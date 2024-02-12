// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;

/// <summary>
/// A custom control for the debugger top navigation bar.
/// </summary>
[ObservableObject]
public sealed partial class TopNavBar
{
    // TODO(abdes): avoid using Ioc.Default, set router in control
    private static readonly DependencyProperty RouterProperty = DependencyProperty.Register(
        nameof(Router),
        typeof(IRouter),
        typeof(TopNavBar),
        new PropertyMetadata(Ioc.Default.GetRequiredService<IRouter>()));

    [ObservableProperty]
    private string url;

    public TopNavBar()
    {
        this.InitializeComponent();

        // Get the current navigation URL
        // TODO(abdes) cleanup how to get the current Url from the router
        this.Url = this.GetCurrentNavigationUrl();

        // TODO(abdes): Register for router url changes to update the Url property
    }

    public IRouter Router
    {
        get => (IRouter)this.GetValue(RouterProperty);
        set
        {
            this.SetValue(RouterProperty, value);
            this.Url = this.GetCurrentNavigationUrl();
        }
    }

    private string GetCurrentNavigationUrl()
        => this.Router.GetCurrentUrlTreeForTarget(Target.Self)?.ToString() ?? string.Empty;

    private void UrlTextBox_KeyUp(object sender, KeyRoutedEventArgs e)
    {
        _ = sender;

        if (e.Key == VirtualKey.Enter)
        {
            if (sender is TextBox control)
            {
                var text = control.Text;
            }

            this.Router.Navigate(this.Url);
        }
    }
}
