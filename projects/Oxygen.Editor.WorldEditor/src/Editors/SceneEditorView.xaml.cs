// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Editors;

[ViewModel(typeof(SceneEditorViewModel))]
public sealed partial class SceneEditorView : UserControl
{
    public SceneEditorView()
    {
        this.InitializeComponent();
    }
}
