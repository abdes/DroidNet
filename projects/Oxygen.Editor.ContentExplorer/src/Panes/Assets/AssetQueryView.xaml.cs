// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Provides the shared browser search and filter controls.</summary>
[ViewModel(typeof(AssetBrowserQuery))]
public sealed partial class AssetQueryView
{
    /// <summary>Initializes a new instance of the <see cref="AssetQueryView"/> class.</summary>
    public AssetQueryView() => this.InitializeComponent();
}
