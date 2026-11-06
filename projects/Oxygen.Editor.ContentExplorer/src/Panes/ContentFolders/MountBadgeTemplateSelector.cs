// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;
using DataTemplate = Microsoft.UI.Xaml.DataTemplate;
using DependencyObject = Microsoft.UI.Xaml.DependencyObject;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>
///     Gives a mount row its badge: read-only sources (the Cooked and Build output folders, local libraries and paks)
///     a lock, so derived output is never mistaken for authored content; a source mount a <c>.mounts.json</c> binds a
///     "This machine" badge; and a mount this machine cannot read a caution, whose tooltip says why.
/// </summary>
public partial class MountBadgeTemplateSelector : DataTemplateSelector
{
    /// <summary>Gets or sets the badge template for read-only mounted sources.</summary>
    public DataTemplate? ReadOnlyTemplate { get; set; }

    /// <summary>Gets or sets the badge template for a source mount bound on this machine.</summary>
    public DataTemplate? ThisMachineTemplate { get; set; }

    /// <summary>Gets or sets the badge template for a mount this machine cannot read.</summary>
    public DataTemplate? UnavailableTemplate { get; set; }

    /// <inheritdoc />
    protected override DataTemplate? SelectTemplateCore(object item, DependencyObject container)
        => item switch
        {
            VirtualFolderMountTreeItemAdapter or LibraryMountTreeItemAdapter => this.ReadOnlyTemplate,
            AuthoringMountPointTreeItemAdapter { IsBoundOnThisMachine: true } => this.ThisMachineTemplate,
            UnavailableMountTreeItemAdapter => this.UnavailableTemplate,
            _ => null,
        };
}
