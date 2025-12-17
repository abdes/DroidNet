// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
///     Represents the view for editing a scene node in the properties editor.
/// </summary>
[ViewModel(typeof(SceneNodeEditorViewModel))]
public sealed partial class SceneNodeEditorView : UserControl
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNodeEditorView" /> class.
    /// </summary>
    public SceneNodeEditorView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, _) =>
        {
            if (this.ViewModel is null)
            {
                return;
            }

            this.Resources["VmToViewConverter"] = this.ViewModel.VmToViewConverter;
        };
    }
}
