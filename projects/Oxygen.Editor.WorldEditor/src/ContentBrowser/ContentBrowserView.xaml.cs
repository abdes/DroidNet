// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// The View for the Content Browser UI.
/// </summary>
[ViewModel(typeof(ContentBrowserViewModel))]
public sealed partial class ContentBrowserView : UserControl
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ContentBrowserView"/> class.
    /// </summary>
    public ContentBrowserView()
    {
        this.InitializeComponent();
    }
}
