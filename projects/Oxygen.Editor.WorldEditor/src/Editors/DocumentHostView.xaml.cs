// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.Editors;

/// <summary>
///     Represents the host view for document editors in the World Editor.
/// </summary>
[ViewModel(typeof(DocumentHostViewModel))]
public sealed partial class DocumentHostView : UserControl
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentHostView"/> class.
    /// </summary>
    public DocumentHostView()
    {
        this.InitializeComponent();
    }
}
