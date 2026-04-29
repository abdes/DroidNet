// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Directional light inspector view.
/// </summary>
[ViewModel(typeof(DirectionalLightViewModel))]
public sealed partial class DirectionalLightView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightView"/> class.
    /// </summary>
    public DirectionalLightView()
    {
        this.InitializeComponent();
    }

    private void ColorPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        _ = sender;
        this.ViewModel?.SetColor(args.NewColor);
    }
}
