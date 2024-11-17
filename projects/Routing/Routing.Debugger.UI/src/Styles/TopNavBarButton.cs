// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.Styles;

/// <summary>
/// A custom button to be used for the top navigation bar.
/// </summary>
public partial class TopNavBarButton : Button
{
    private static readonly DependencyProperty TooltipProperty = DependencyProperty.Register(
        nameof(Tooltip),
        typeof(string),
        typeof(TopNavBarButton),
        new PropertyMetadata(default(string)));

    /// <summary>
    /// Initializes a new instance of the <see cref="TopNavBarButton"/> class.
    /// </summary>
    public TopNavBarButton()
    {
        this.Style = (Style)Application.Current.Resources[nameof(TopNavBarButton)];
    }

    /// <summary>
    /// Gets or sets the tooltip text for the button.
    /// </summary>
    /// <value>
    /// The tooltip text to be displayed.
    /// </value>
    /// <remarks>
    /// When the tooltip text is set, a <see cref="ToolTip"/> is created and assigned to the button.
    /// </remarks>
    public string Tooltip
    {
        get => (string)this.GetValue(TooltipProperty);
        set
        {
            this.SetValue(TooltipProperty, value);

            var toolTip = new ToolTip
            {
                Content = value,
                VerticalOffset = -60,
            };
            ToolTipService.SetToolTip(this, toolTip);
        }
    }
}
