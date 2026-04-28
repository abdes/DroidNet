// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Material editor document view.
/// </summary>
[ViewModel(typeof(MaterialEditorViewModel))]
public sealed partial class MaterialEditorView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditorView"/> class.
    /// </summary>
    public MaterialEditorView()
    {
        this.InitializeComponent();
    }

    private void BaseColorPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
    {
        _ = sender;
        this.ViewModel?.SetBaseColor(args.NewColor);
    }
}
