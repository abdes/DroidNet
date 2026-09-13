// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;
using Oxygen.Editor.ContentBrowser.AssetIdentity;

namespace Oxygen.Editor.ContentBrowser.Panes.Assets;

/// <summary>Displays supplemental asset information in a tooltip without reserving browser space.</summary>
[ViewModel(typeof(AssetInformation))]
public sealed partial class AssetInformationView
{
    /// <summary>Initializes a new instance of the <see cref="AssetInformationView"/> class.</summary>
    public AssetInformationView() => this.InitializeComponent();
}
