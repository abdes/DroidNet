// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.ViewModels;

namespace Oxygen.Editor.WorldEditor.Views;

[ViewModel(typeof(RendererViewModel))]
public sealed partial class RendererView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RendererView"/> class.
    /// </summary>
    public RendererView()
    {
        this.InitializeComponent();
    }
}
