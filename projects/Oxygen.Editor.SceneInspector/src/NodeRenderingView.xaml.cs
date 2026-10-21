// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Node Rendering section view.</summary>
[ViewModel(typeof(NodeRenderingViewModel))]
public sealed partial class NodeRenderingView
{
    /// <summary>Initializes a new instance of the <see cref="NodeRenderingView"/> class.</summary>
    public NodeRenderingView() => this.InitializeComponent();
}
