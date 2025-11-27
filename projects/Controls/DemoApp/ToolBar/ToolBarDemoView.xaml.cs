// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.ToolBar;

/// <summary>
/// Demo page for the ToolBar control.
/// </summary>
[ViewModel(typeof(ToolBarDemoViewModel))]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "Views that get injected with the ViewModel must be public")]
public sealed partial class ToolBarDemoView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ToolBarDemoView"/> class.
    /// </summary>
    public ToolBarDemoView()
    {
        this.InitializeComponent();
    }

    private void LabelPosition_Changed(object sender, RoutedEventArgs e)
    {
        if (sender is RadioButton rb && rb.Tag is string tag)
        {
            _ = this.ViewModel?.DefaultLabelPosition = tag switch
            {
                "Collapsed" => ToolBarLabelPosition.Collapsed,
                "Right" => ToolBarLabelPosition.Right,
                "Bottom" => ToolBarLabelPosition.Bottom,
                "Left" => ToolBarLabelPosition.Left,
                "Top" => ToolBarLabelPosition.Top,
                _ => ToolBarLabelPosition.Right,
            };
        }
    }
}
