// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Mvvm.Generators;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Perspective camera inspector view.
/// </summary>
[ViewModel(typeof(PerspectiveCameraViewModel))]
public sealed partial class PerspectiveCameraView
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PerspectiveCameraView"/> class.
    /// </summary>
    public PerspectiveCameraView()
    {
        this.InitializeComponent();
    }
}
