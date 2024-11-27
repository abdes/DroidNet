// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// A <see cref="DataTemplateSelector"/> that returns the correct template to be used based on whether
/// the item being displayed is an <see cref="IFolder"/> or an <see cref="IDocument"/>.
/// </summary>
internal sealed partial class FileListDataTemplateSelector : DataTemplateSelector
{
    // Initialized as static resource in the XAML
#pragma warning disable CS8618
    /// <summary>
    /// Gets or sets the template to be used for folders.
    /// </summary>
    public DataTemplate FolderTemplate { get; set; }
#pragma warning restore CS8618

    // Initialized as static resource in the XAML
#pragma warning disable CS8618
    /// <summary>
    /// Gets or sets the template to be used for files.
    /// </summary>
    public DataTemplate FileTemplate { get; set; }
#pragma warning restore CS8618

    /// <inheritdoc/>
    protected override DataTemplate SelectTemplateCore(object item)
        => this.SelectTemplateCore(item, container: null);

    /// <inheritdoc/>
    protected override DataTemplate SelectTemplateCore(object item, DependencyObject? container)
        => item is IFolder ? this.FolderTemplate : this.FileTemplate;
}
