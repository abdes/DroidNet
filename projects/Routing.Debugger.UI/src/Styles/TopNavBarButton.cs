// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Styles;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class TopNavBarButton : Button
{
    private static readonly DependencyProperty TooltipProperty = DependencyProperty.Register(
        nameof(Tooltip),
        typeof(string),
        typeof(TopNavBarButton),
        new PropertyMetadata(default(string)));

    public TopNavBarButton() => this.Style = (Style)Application.Current.Resources[nameof(TopNavBarButton)];

    public string Tooltip
    {
        get => (string)this.GetValue(TooltipProperty);
        set
        {
            this.SetValue(TooltipProperty, value);

            var toolTip = new ToolTip { Content = value, VerticalOffset = -60 };
            ToolTipService.SetToolTip(this, toolTip);
        }
    }
}
