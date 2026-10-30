// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Shows where a new mount's folder is saved, in the dialogs that add a mount.</summary>
public sealed partial class MountPlacementView : UserControl
{
    /// <summary>Identifies the <see cref="Placement"/> dependency property.</summary>
    public static readonly DependencyProperty PlacementProperty = DependencyProperty.Register(
        nameof(Placement),
        typeof(MountPlacement),
        typeof(MountPlacementView),
        new PropertyMetadata(defaultValue: null, OnPlacementChanged));

    /// <summary>Initializes a new instance of the <see cref="MountPlacementView"/> class.</summary>
    public MountPlacementView() => this.InitializeComponent();

    /// <summary>Gets or sets the placement the view edits.</summary>
    public MountPlacement? Placement
    {
        get => (MountPlacement?)this.GetValue(PlacementProperty);
        set => this.SetValue(PlacementProperty, value);
    }

    private static void OnPlacementChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
        => ((MountPlacementView)sender).Bindings.Update();
}
