// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene environment inspector view.
/// </summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="EnvironmentView"/> class.
    /// </summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
    }

    private void BackgroundPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        _ = sender;
        this.ViewModel?.SetBackgroundColor(args.NewColor);
    }
}
