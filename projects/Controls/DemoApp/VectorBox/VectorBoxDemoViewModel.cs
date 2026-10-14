// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls.Demo.InPlaceEdit;

/// <summary>
/// ViewModel for the <see cref="VectorBoxDemoView" /> view.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel must be public")]
public partial class VectorBoxDemoViewModel : ObservableObject
{
    [ObservableProperty]
    public partial double PositionX { get; set; } = 1.5;

    [ObservableProperty]
    public partial double PositionY { get; set; } = 2.3;

    [ObservableProperty]
    public partial double PositionZ { get; set; } = 3.7;

    [ObservableProperty]
    public partial double RotationX { get; set; }

    [ObservableProperty]
    public partial double RotationY { get; set; } = 45.5;

    [ObservableProperty]
    public partial double RotationZ { get; set; } = 90;

    [ObservableProperty]
    public partial double ScaleX { get; set; } = 1;

    [ObservableProperty]
    public partial double ScaleY { get; set; } = 1;

    [ObservableProperty]
    public partial double ScaleZ { get; set; } = 1;

    [ObservableProperty]
    public partial double OrientationX { get; set; }

    [ObservableProperty]
    public partial double OrientationY { get; set; } = 0.383;

    [ObservableProperty]
    public partial double OrientationZ { get; set; }

    [ObservableProperty]
    public partial double OrientationW { get; set; } = 0.924;

    [ObservableProperty]
    public partial double TilingU { get; set; } = 2;

    [ObservableProperty]
    public partial double TilingV { get; set; } = 1;

    [ObservableProperty]
    public partial double MixedMaskX { get; set; } = -15.7;

    [ObservableProperty]
    public partial double MixedMaskY { get; set; } = 23.45;

    [ObservableProperty]
    public partial double MixedMaskZ { get; set; } = 0.123;

    [ObservableProperty]
    public partial double ConstrainedX { get; set; } = -45;

    [ObservableProperty]
    public partial double ConstrainedY { get; set; }

    [ObservableProperty]
    public partial double ConstrainedZ { get; set; } = 120;

    [ObservableProperty]
    public partial double MixedX { get; set; } = 10;

    [ObservableProperty]
    public partial double MixedY { get; set; } = 20;

    [ObservableProperty]
    public partial double MixedZ { get; set; } = 30;

    [ObservableProperty]
    public partial bool IsMixedX { get; set; }

    [ObservableProperty]
    public partial bool IsMixedY { get; set; } = true;

    [ObservableProperty]
    public partial bool IsMixedZ { get; set; }
}
