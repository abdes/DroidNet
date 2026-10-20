// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The view of a window's document area: the active document's editor, under the banners of its file's state.
/// </summary>
[ViewModel(typeof(DocumentAreaViewModel))]
public sealed partial class DocumentAreaView : UserControl
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentAreaView"/> class.
    /// </summary>
    public DocumentAreaView()
    {
        this.InitializeComponent();
    }
}
