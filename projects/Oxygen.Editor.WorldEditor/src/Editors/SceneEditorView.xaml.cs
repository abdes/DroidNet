// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Represents the view for editing scenes in the Oxygen World Editor.
/// </summary>
[ViewModel(typeof(SceneEditorViewModel))]
public sealed partial class SceneEditorView : UserControl
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneEditorView"/> class.
    /// </summary>
    public SceneEditorView()
    {
        this.InitializeComponent();
    }
}
