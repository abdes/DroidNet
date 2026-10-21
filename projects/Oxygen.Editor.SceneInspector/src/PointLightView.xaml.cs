// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Point light inspector view.</summary>
[ViewModel(typeof(PointLightViewModel))]
public sealed partial class PointLightView
{
    /// <summary>Initializes a new instance of the <see cref="PointLightView"/> class.</summary>
    public PointLightView() => this.InitializeComponent();
}
