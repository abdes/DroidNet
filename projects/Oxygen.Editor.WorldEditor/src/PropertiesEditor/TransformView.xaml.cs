// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

/// <summary>
///     Represents the view for editing transform properties in the World Editor.
/// </summary>
[ViewModel(typeof(TransformViewModel))]
public partial class TransformView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="TransformView"/> class.
    /// </summary>
    public TransformView()
    {
        this.InitializeComponent();
    }
}
