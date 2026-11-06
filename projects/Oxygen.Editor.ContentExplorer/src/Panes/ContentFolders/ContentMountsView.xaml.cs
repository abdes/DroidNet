// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Displays the Content mounts dialog: the mount list and the form that adds one.</summary>
[ViewModel(typeof(ContentMountsViewModel))]
public sealed partial class ContentMountsView
{
    /// <summary>Initializes a new instance of the <see cref="ContentMountsView"/> class.</summary>
    public ContentMountsView() => this.InitializeComponent();

    /// <summary>Hides what shows only when a value is false.</summary>
    /// <param name="value">The value.</param>
    /// <returns>The visibility.</returns>
    public static Visibility CollapsedWhen(bool value) => value ? Visibility.Collapsed : Visibility.Visible;
}
