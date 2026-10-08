// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.ContentBrowser.ProjectExplorer;

/// <summary>
///     Gives mounted sources (Cooked, Imported, Build and local libraries) a read-only badge, so derived output is
///     never mistaken for authored content.
/// </summary>
public partial class ReadOnlyBadgeTemplateSelector : DataTemplateSelector
{
    /// <summary>Gets or sets the badge template for read-only mounted sources.</summary>
    public DataTemplate? ReadOnlyTemplate { get; set; }

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item is VirtualFolderMountTreeItemAdapter ? this.ReadOnlyTemplate : null;
}
