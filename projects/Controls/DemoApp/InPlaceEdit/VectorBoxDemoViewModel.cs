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
    // ============ Basic 3D Vector ============
    [ObservableProperty]
    public partial float Position3DX { get; set; } = 1.5f;

    [ObservableProperty]
    public partial float Position3DY { get; set; } = 2.3f;

    [ObservableProperty]
    public partial float Position3DZ { get; set; } = 3.7f;

    // ============ 2D Vector ============
    [ObservableProperty]
    public partial float Scale2DX { get; set; } = 1.0f;

    [ObservableProperty]
    public partial float Scale2DY { get; set; } = 1.0f;

    // ============ Rotation Vector (with mask) ============
    [ObservableProperty]
    public partial float RotationX { get; set; } = 0.0f;

    [ObservableProperty]
    public partial float RotationY { get; set; } = 45.5f;

    [ObservableProperty]
    public partial float RotationZ { get; set; } = 90.0f;

    // ============ Mixed Mask Vector ============
    [ObservableProperty]
    public partial float MixedMaskX { get; set; } = -15.7f;

    [ObservableProperty]
    public partial float MixedMaskY { get; set; } = 23.45f;

    [ObservableProperty]
    public partial float MixedMaskZ { get; set; } = 0.123f;

    // ============ Fast Adjust Vector (with Multiplier) ============
    [ObservableProperty]
    public partial float SensitivityX { get; set; } = 0.5f;

    [ObservableProperty]
    public partial float SensitivityY { get; set; } = 1.25f;

    [ObservableProperty]
    public partial float SensitivityZ { get; set; } = 0.75f;

    // ============ Constrained Vector (with validation) ============
    [ObservableProperty]
    public partial float ConstrainedX { get; set; } = -45.0f;

    [ObservableProperty]
    public partial float ConstrainedY { get; set; } = 0.0f;

    [ObservableProperty]
    public partial float ConstrainedZ { get; set; } = 120.0f;

    // ============ Indeterminate Vector ============
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial float IndeterminateX { get; set; } = 10.0f;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial float IndeterminateY { get; set; } = 20.0f;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial float IndeterminateZ { get; set; } = 30.0f;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial bool IsIndeterminateX { get; set; } = false;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial bool IsIndeterminateY { get; set; } = false;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IndeterminateStatusText))]
    public partial bool IsIndeterminateZ { get; set; } = false;

    /// <summary>
    /// Gets a formatted status string describing which components are indeterminate.
    /// Useful for displaying state in multi-selection scenarios.
    /// </summary>
    public string IndeterminateStatusText
    {
        get
        {
            var indeterminate = new List<string>();
            if (this.IsIndeterminateX)
            {
                indeterminate.Add("X");
            }

            if (this.IsIndeterminateY)
            {
                indeterminate.Add("Y");
            }

            if (this.IsIndeterminateZ)
            {
                indeterminate.Add("Z");
            }

            if (indeterminate.Count == 0)
            {
                return "All components have definite values.";
            }

            return $"Component(s) {string.Join(", ", indeterminate)} showing indeterminate '-.-' (mixed values in multi-selection).";
        }
    }
}
