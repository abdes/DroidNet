// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.
namespace Oxygen.Editor.WorldEditor.PropertiesEditor;

[ViewModel(typeof(PropertiesEditorViewModel))]
public sealed partial class PropertiesEditorView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PropertiesEditorView"/> class.
    /// </summary>
    public PropertiesEditorView()
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
