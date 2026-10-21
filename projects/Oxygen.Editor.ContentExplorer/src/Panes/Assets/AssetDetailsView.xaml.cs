// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Shows the details pane: the selected asset's preview, status and facts, or a multi-selection summary.</summary>
[ViewModel(typeof(AssetDetailsViewModel))]
public sealed partial class AssetDetailsView
{
    /// <summary>Initializes a new instance of the <see cref="AssetDetailsView"/> class.</summary>
    public AssetDetailsView() => this.InitializeComponent();
}
