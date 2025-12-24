// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     A <see cref="DataTemplateSelector" /> that can map a <see cref="FolderTreeItemAdapter" /> to a template that can be
///     used to
///     display a <see cref="Thumbnail" /> for it.
/// </summary>
public partial class ThumbnailTemplateSelector : DataTemplateSelector
{
    /// <summary>
    ///     Gets or sets the default template to use when no specific template is selected.
    /// </summary>
    public DataTemplate? DefaultTemplate { get; set; }

    /// <summary>
    ///     Gets or sets the template used for <see cref="FolderTreeItemAdapter"/> items.
    /// </summary>
    public DataTemplate? FolderTemplate { get; set; }

    /// <summary>
    ///     Gets or sets the template used for <see cref="ProjectRootTreeItemAdapter"/> items.
    /// </summary>
    public DataTemplate? ProjectRootTemplate { get; set; }

    /// <summary>
    ///     Gets or sets the template used for <see cref="AuthoringMountPointTreeItemAdapter"/> items.
    /// </summary>
    public DataTemplate? AuthoringMountTemplate { get; set; }

    /// <summary>
    ///     Gets or sets the template used for <see cref="VirtualFolderMountTreeItemAdapter"/> items.
    /// </summary>
    public DataTemplate? VirtualFolderMountTemplate { get; set; }

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item switch
        {
            FolderTreeItemAdapter => this.FolderTemplate ?? this.DefaultTemplate,
            ProjectRootTreeItemAdapter => this.ProjectRootTemplate ?? this.DefaultTemplate,
            AuthoringMountPointTreeItemAdapter => this.AuthoringMountTemplate ?? this.DefaultTemplate,
            VirtualFolderMountTreeItemAdapter => this.VirtualFolderMountTemplate ?? this.DefaultTemplate,
            _ => this.DefaultTemplate,
        };
}
