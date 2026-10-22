// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// The page of a document window: the document whose tab is selected there.
/// </summary>
[ViewModel(typeof(DocumentPageViewModel))]
public sealed partial class DocumentPageView : Page
{
    /// <summary>
    /// Initializes a new instance of the <see cref="DocumentPageView"/> class.
    /// </summary>
    public DocumentPageView()
    {
        this.InitializeComponent();
    }
}
